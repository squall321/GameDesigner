// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Manager/Audio_SoundManagerSubsystem.h"
#include "DesignPatternsAudioModule.h"
#include "Data/Audio_SoundBankDataAsset.h"
#include "Mix/Audio_MixController.h"
#include "Mix/Audio_MixProfileDataAsset.h"
#include "Mix/Audio_DuckBusDataAsset.h"
#include "Settings/Audio_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Kismet/GameplayStatics.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UAudio_SoundManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the locator + registry + bus to exist before we register/listen.
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	Collection.InitializeDependency(UDP_DataRegistrySubsystem::StaticClass());
	Collection.InitializeDependency(UDP_MessageBusSubsystem::StaticClass());

	// Detect audio availability once. On a dedicated server / -nosound there is no device, so every
	// play path becomes a guarded no-op while bookkeeping (counts, mix stack) still behaves.
	const UWorld* World = GetWorld();
	bAudioAvailable = (World != nullptr) && (World->GetNetMode() != NM_DedicatedServer)
		&& (GEngine != nullptr) && GEngine->UseSound();

	// Owned mix controller (instanced subobject; outer is this subsystem so GC tracks it).
	MixController = NewObject<UAudio_MixController>(this, UAudio_MixController::StaticClass(), TEXT("Audio_MixController"));

	// Seed runtime category volumes from settings so the first play already honours per-category trim.
	if (const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get())
	{
		for (const TPair<FGameplayTag, float>& Pair : Settings->CategoryDefaultVolumes)
		{
			if (Pair.Key.IsValid())
			{
				CategoryVolumes.Add(Pair.Key, FMath::Max(0.f, Pair.Value));
			}
		}
	}

	LoadDefaultBanksFromSettings();
	RegisterAsService();
	BindBusListeners();

	UE_LOG(LogDP, Log, TEXT("Audio_SoundManagerSubsystem initialized (audio %s, %d bank(s))."),
		bAudioAvailable ? TEXT("available") : TEXT("unavailable"), LoadedBanks.Num());
}

void UAudio_SoundManagerSubsystem::Deinitialize()
{
	// Stop listening and unregister so a torn-down world's manager cannot be resolved.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	UnregisterAsService();

	// Cancel any in-flight async loads.
	for (TPair<FGameplayTag, TSharedPtr<FStreamableHandle>>& Pending : PendingLoads)
	{
		if (Pending.Value.IsValid())
		{
			Pending.Value->CancelHandle();
		}
	}
	PendingLoads.Reset();

	// Stop and drop tracked voices.
	for (const FActiveVoice& Voice : ActiveVoices)
	{
		if (UAudioComponent* Comp = Voice.Component.Get())
		{
			Comp->Stop();
		}
	}
	ActiveVoices.Reset();

	if (MixController)
	{
		MixController->ClearAll();
		MixController = nullptr;
	}

	Super::Deinitialize();
}

// =====================================================================================================
// Service / bus / bank wiring
// =====================================================================================================

void UAudio_SoundManagerSubsystem::RegisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the GameInstance owns this subsystem's lifetime; the locator only observes,
		// so it can never extend a dead subsystem's life.
		Locator->RegisterService(AudioNativeTags::Service_Audio, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UAudio_SoundManagerSubsystem::UnregisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->UnregisterService(AudioNativeTags::Service_Audio);
	}
}

void UAudio_SoundManagerSubsystem::BindBusListeners()
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	TWeakObjectPtr<UAudio_SoundManagerSubsystem> WeakThis(this);

	// DP.Bus.Audio.Play -> route a play request.
	Bus->ListenNative(AudioNativeTags::Bus_Play,
		[WeakThis](const FDP_Message& Message)
		{
			UAudio_SoundManagerSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (const FAudio_PlayRequest* Req = Message.Payload.GetPtr<FAudio_PlayRequest>())
			{
				if (Req->bAtLocation)
				{
					Self->PlaySoundAtLocation_Implementation(Req->SoundTag, Req->Location, Req->VolumeMult);
				}
				else
				{
					Self->PlaySound2D_Implementation(Req->SoundTag, Req->VolumeMult);
				}
			}
		},
		this, EDP_MessageMatch::ExactOrChild);

	// DP.Bus.Audio.Mix -> push/pop a mix profile by tag (fire-and-forget; handles tracked per tag).
	Bus->ListenNative(AudioNativeTags::Bus_Mix,
		[WeakThis](const FDP_Message& Message)
		{
			UAudio_SoundManagerSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (const FAudio_MixRequest* Req = Message.Payload.GetPtr<FAudio_MixRequest>())
			{
				if (!Req->MixTag.IsValid())
				{
					return;
				}
				if (Req->bPush)
				{
					const FGuid Handle = Self->PushMixProfile(Req->MixTag, Req->PriorityOverride);
					if (Handle.IsValid())
					{
						Self->BusMixHandles.FindOrAdd(Req->MixTag).Add(Handle);
					}
				}
				else
				{
					// Pop the most-recent (LIFO) bus push of this tag, if any.
					if (TArray<FGuid>* Handles = Self->BusMixHandles.Find(Req->MixTag))
					{
						if (Handles->Num() > 0)
						{
							const FGuid Handle = Handles->Pop();
							Self->PopMixProfile(Handle);
							if (Handles->Num() == 0)
							{
								Self->BusMixHandles.Remove(Req->MixTag);
							}
						}
					}
				}
			}
		},
		this, EDP_MessageMatch::ExactOrChild);

	// DP.Bus.Audio.CategoryVolume -> retune a category.
	Bus->ListenNative(AudioNativeTags::Bus_CategoryVolume,
		[WeakThis](const FDP_Message& Message)
		{
			UAudio_SoundManagerSubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			if (const FAudio_CategoryVolumeRequest* Req = Message.Payload.GetPtr<FAudio_CategoryVolumeRequest>())
			{
				Self->SetCategoryVolume_Implementation(Req->Category, Req->Volume);
			}
		},
		this, EDP_MessageMatch::ExactOrChild);
}

void UAudio_SoundManagerSubsystem::LoadDefaultBanksFromSettings()
{
	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	for (const TSoftObjectPtr<UAudio_SoundBankDataAsset>& SoftBank : Settings->DefaultSoundBanks)
	{
		if (SoftBank.IsNull())
		{
			continue;
		}
		// Banks themselves are small (they hold only soft refs), so a synchronous load at startup is
		// cheap and keeps the first play deterministic. The sounds inside stay unloaded (soft).
		if (UAudio_SoundBankDataAsset* Bank = SoftBank.LoadSynchronous())
		{
			AddSoundBank(Bank);
		}
	}
}

void UAudio_SoundManagerSubsystem::AddSoundBank(UAudio_SoundBankDataAsset* Bank)
{
	if (!Bank || LoadedBanks.Contains(Bank))
	{
		return;
	}
	LoadedBanks.Add(Bank);
	UE_LOG(LogDP, Verbose, TEXT("Audio: added sound bank '%s' (%d entries)."),
		*Bank->DataTag.ToString(), Bank->Entries.Num());
}

void UAudio_SoundManagerSubsystem::RemoveSoundBank(UAudio_SoundBankDataAsset* Bank)
{
	LoadedBanks.Remove(Bank);
}

// =====================================================================================================
// IAudio_AudioController
// =====================================================================================================

void UAudio_SoundManagerSubsystem::PlaySound2D_Implementation(FGameplayTag SoundTag, float VolumeMult)
{
	PlayInternal(SoundTag, VolumeMult, /*bAtLocation=*/false, FVector::ZeroVector);
}

void UAudio_SoundManagerSubsystem::PlaySoundAtLocation_Implementation(FGameplayTag SoundTag, FVector Location, float VolumeMult)
{
	PlayInternal(SoundTag, VolumeMult, /*bAtLocation=*/true, Location);
}

void UAudio_SoundManagerSubsystem::StopCategory_Implementation(FGameplayTag Category)
{
	if (!Category.IsValid())
	{
		return;
	}
	PruneFinishedVoices();

	for (int32 Index = ActiveVoices.Num() - 1; Index >= 0; --Index)
	{
		const FActiveVoice& Voice = ActiveVoices[Index];
		// Match the requested category OR any child of it (tag hierarchy).
		if (Voice.Category.IsValid() && Voice.Category.MatchesTag(Category))
		{
			if (UAudioComponent* Comp = Voice.Component.Get())
			{
				Comp->Stop();
			}
			ActiveVoices.RemoveAt(Index);
		}
	}
}

void UAudio_SoundManagerSubsystem::SetCategoryVolume_Implementation(FGameplayTag Category, float Volume)
{
	if (!Category.IsValid())
	{
		return;
	}
	const float Clamped = FMath::Max(0.f, Volume);
	CategoryVolumes.Add(Category, Clamped);
	RefreshCategoryVoiceVolumes(Category);
}

// =====================================================================================================
// Play pipeline
// =====================================================================================================

void UAudio_SoundManagerSubsystem::PlayInternal(const FGameplayTag& SoundTag, float VolumeMult, bool bAtLocation, const FVector& Location)
{
	if (!bAudioAvailable || !SoundTag.IsValid())
	{
		return; // Headless / no-device, or no tag: safe no-op.
	}

	const FAudio_SoundEntry* Entry = ResolveEntry(SoundTag);
	if (!Entry)
	{
		UE_LOG(LogDP, Verbose, TEXT("Audio: PlaySound for unresolved tag '%s' (no bank entry)."), *SoundTag.ToString());
		return;
	}
	if (Entry->Sound.IsNull())
	{
		UE_LOG(LogDP, Warning, TEXT("Audio: bank entry '%s' has no Sound assigned."), *SoundTag.ToString());
		return;
	}

	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	const float Master = Settings ? FMath::Max(0.f, Settings->MasterVolume) : 1.f; // Defensive: unity if CDO missing.

	// The non-category portion of the voice volume; category + duck are applied at spawn so a runtime
	// category-volume change can re-scale live voices consistently.
	const float BaseVolume = FMath::Max(0.f, VolumeMult) * FMath::Max(0.f, Entry->DefaultVolume) * Master;

	const TSoftObjectPtr<USoundAttenuation> Attenuation = bAtLocation ? Entry->Attenuation : TSoftObjectPtr<USoundAttenuation>();

	// Fast path: already loaded.
	if (USoundBase* LoadedSound = Entry->Sound.Get())
	{
		OnSoundReadyAndPlay(SoundTag, BaseVolume, bAtLocation, Location, Attenuation);
		return;
	}

	// Coalesce concurrent loads of the same tag: if one is already in flight, let it complete and play.
	if (PendingLoads.Contains(SoundTag))
	{
		return;
	}

	// Async-load the soft sound (and attenuation) then play. Captures weak-this so a destroyed manager
	// during the load simply drops the callback.
	TWeakObjectPtr<UAudio_SoundManagerSubsystem> WeakThis(this);
	const FGameplayTag CapturedTag = SoundTag;
	const FVector CapturedLoc = Location;

	TArray<FSoftObjectPath> ToLoad;
	ToLoad.Add(Entry->Sound.ToSoftObjectPath());
	if (!Attenuation.IsNull())
	{
		ToLoad.Add(Attenuation.ToSoftObjectPath());
	}

	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
	TSharedPtr<FStreamableHandle> Handle = Streamable.RequestAsyncLoad(
		ToLoad,
		FStreamableDelegate::CreateLambda(
			[WeakThis, CapturedTag, BaseVolume, bAtLocation, CapturedLoc, Attenuation]()
			{
				if (UAudio_SoundManagerSubsystem* Self = WeakThis.Get())
				{
					Self->PendingLoads.Remove(CapturedTag);
					Self->OnSoundReadyAndPlay(CapturedTag, BaseVolume, bAtLocation, CapturedLoc, Attenuation);
				}
			}),
		FStreamableManager::DefaultAsyncLoadPriority);

	if (Handle.IsValid())
	{
		PendingLoads.Add(SoundTag, Handle);
	}
}

void UAudio_SoundManagerSubsystem::OnSoundReadyAndPlay(FGameplayTag SoundTag, float BaseVolume, bool bAtLocation, FVector Location,
	TSoftObjectPtr<USoundAttenuation> Attenuation)
{
	if (!bAudioAvailable || !GetWorld())
	{
		return; // World may have torn down during the async load.
	}

	const FAudio_SoundEntry* Entry = ResolveEntry(SoundTag);
	if (!Entry)
	{
		return; // Bank removed during load.
	}
	USoundBase* Sound = Entry->Sound.Get();
	if (!Sound)
	{
		return; // Load failed.
	}

	PruneFinishedVoices();

	// Concurrency: ensure room (per-entry MaxConcurrent + per-category cap) via oldest-steal.
	if (!MakeRoomForVoice(SoundTag, Entry->Category, Entry->MaxConcurrent))
	{
		// No room even after stealing (e.g. cap is 0/this very voice would be the oldest): drop it.
		UE_LOG(LogDP, Verbose, TEXT("Audio: dropped voice '%s' (category at capacity)."), *SoundTag.ToString());
		return;
	}

	USoundAttenuation* Atten = Attenuation.IsNull() ? nullptr : Attenuation.Get();
	SpawnVoice(Sound, BaseVolume, bAtLocation, Location, SoundTag, Entry->Category, Atten);
}

UAudioComponent* UAudio_SoundManagerSubsystem::SpawnVoice(USoundBase* Sound, float BaseVolume, bool bAtLocation, const FVector& Location,
	const FGameplayTag& SoundTag, const FGameplayTag& Category, USoundAttenuation* Attenuation)
{
	UWorld* World = GetWorld();
	if (!World || !Sound)
	{
		return nullptr;
	}

	// Effective volume = base * category multiplier * active duck for that category.
	const float CategoryVol = ResolveCategoryVolume(Category);
	const float Duck = MixController ? MixController->GetActiveDuckVolume(Category) : 1.f;
	const float FinalVolume = BaseVolume * CategoryVol * Duck;

	UAudioComponent* Component = nullptr;
	if (bAtLocation)
	{
		// WRAP the engine: SpawnSoundAtLocation gives us a trackable component for spatialized one-shots.
		Component = UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator,
			FinalVolume, /*PitchMultiplier=*/1.f, /*StartTime=*/0.f, Attenuation, /*ConcurrencySettings=*/nullptr, /*bAutoDestroy=*/true);
	}
	else
	{
		// SpawnSound2D returns a component (unlike PlaySound2D) so we can track and stop it.
		Component = UGameplayStatics::SpawnSound2D(World, Sound, FinalVolume, /*PitchMultiplier=*/1.f,
			/*StartTime=*/0.f, /*ConcurrencySettings=*/nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/true);
	}

	if (!Component)
	{
		return nullptr; // Device may have been pulled mid-game; safe.
	}

	FActiveVoice Voice;
	Voice.Component = Component;
	Voice.SoundTag = SoundTag;
	Voice.Category = Category;
	Voice.BaseVolume = BaseVolume;
	Voice.Sequence = NextVoiceSequence++;
	ActiveVoices.Add(MoveTemp(Voice));

	return Component;
}

bool UAudio_SoundManagerSubsystem::MakeRoomForVoice(const FGameplayTag& SoundTag, const FGameplayTag& Category, int32 EntryMaxConcurrent)
{
	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();

	// --- Per-entry MaxConcurrent (0 == unlimited) ---
	if (EntryMaxConcurrent > 0)
	{
		// Count and find the oldest voice of THIS sound tag.
		int32 SameTagCount = 0;
		int32 OldestIndex = INDEX_NONE;
		int64 OldestSeq = TNumericLimits<int64>::Max();
		for (int32 Index = 0; Index < ActiveVoices.Num(); ++Index)
		{
			if (ActiveVoices[Index].SoundTag == SoundTag)
			{
				++SameTagCount;
				if (ActiveVoices[Index].Sequence < OldestSeq)
				{
					OldestSeq = ActiveVoices[Index].Sequence;
					OldestIndex = Index;
				}
			}
		}
		if (SameTagCount >= EntryMaxConcurrent && OldestIndex != INDEX_NONE)
		{
			if (UAudioComponent* Comp = ActiveVoices[OldestIndex].Component.Get())
			{
				Comp->Stop(); // Virtualize: steal the oldest of this sound.
			}
			ActiveVoices.RemoveAt(OldestIndex);
		}
	}

	// --- Per-category voice cap (settings; hierarchy-resolved; always positive) ---
	// Defensive fallback cap when settings CDO is null so an unconfigured project can't unbounded-spawn.
	const int32 CategoryCap = Settings ? Settings->ResolveCategoryVoiceCap(Category) : 64;
	if (CategoryCap > 0)
	{
		// Count and find the oldest voice in THIS category (hierarchy-inclusive).
		auto CountCategory = [this, &Category](int32& OutOldestIndex, int64& OutOldestSeq) -> int32
		{
			int32 Count = 0;
			OutOldestIndex = INDEX_NONE;
			OutOldestSeq = TNumericLimits<int64>::Max();
			for (int32 Index = 0; Index < ActiveVoices.Num(); ++Index)
			{
				const FActiveVoice& V = ActiveVoices[Index];
				if (V.Category.IsValid() && Category.IsValid() && V.Category.MatchesTag(Category))
				{
					++Count;
					if (V.Sequence < OutOldestSeq)
					{
						OutOldestSeq = V.Sequence;
						OutOldestIndex = Index;
					}
				}
			}
			return Count;
		};

		int32 OldestIndex = INDEX_NONE;
		int64 OldestSeq = 0;
		int32 InCategory = CountCategory(OldestIndex, OldestSeq);
		// Steal oldest until there is room for one more.
		while (InCategory >= CategoryCap && OldestIndex != INDEX_NONE)
		{
			if (UAudioComponent* Comp = ActiveVoices[OldestIndex].Component.Get())
			{
				Comp->Stop();
			}
			ActiveVoices.RemoveAt(OldestIndex);
			InCategory = CountCategory(OldestIndex, OldestSeq);
		}
		// If after stealing the category is still at/over cap (cap <= 0 handled above), refuse.
		if (InCategory >= CategoryCap)
		{
			return false;
		}
	}

	return true;
}

// =====================================================================================================
// Voice bookkeeping
// =====================================================================================================

void UAudio_SoundManagerSubsystem::PruneFinishedVoices()
{
	for (int32 Index = ActiveVoices.Num() - 1; Index >= 0; --Index)
	{
		const UAudioComponent* Comp = ActiveVoices[Index].Component.Get();
		if (!Comp || !Comp->IsPlaying())
		{
			ActiveVoices.RemoveAt(Index);
		}
	}
}

void UAudio_SoundManagerSubsystem::RefreshCategoryVoiceVolumes(const FGameplayTag& Category)
{
	const float CategoryVol = ResolveCategoryVolume(Category);
	for (const FActiveVoice& Voice : ActiveVoices)
	{
		if (!Voice.Category.IsValid() || !Category.IsValid() || !Voice.Category.MatchesTag(Category))
		{
			continue;
		}
		if (UAudioComponent* Comp = Voice.Component.Get())
		{
			const float Duck = MixController ? MixController->GetActiveDuckVolume(Voice.Category) : 1.f;
			Comp->SetVolumeMultiplier(Voice.BaseVolume * CategoryVol * Duck);
		}
	}
}

float UAudio_SoundManagerSubsystem::ResolveCategoryVolume(const FGameplayTag& Category) const
{
	if (!Category.IsValid())
	{
		return 1.f; // Uncategorized voices are unattenuated by category.
	}
	// Exact, then walk parents so a leaf inherits an ancestor's runtime volume.
	if (const float* Exact = CategoryVolumes.Find(Category))
	{
		return *Exact;
	}
	for (FGameplayTag Parent = Category.RequestDirectParent(); Parent.IsValid(); Parent = Parent.RequestDirectParent())
	{
		if (const float* Found = CategoryVolumes.Find(Parent))
		{
			return *Found;
		}
	}
	// Fall back to the configured category default (or unity if settings missing).
	if (const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get())
	{
		return Settings->ResolveCategoryDefaultVolume(Category);
	}
	return 1.f;
}

int32 UAudio_SoundManagerSubsystem::GetActiveVoiceCount() const
{
	int32 Count = 0;
	for (const FActiveVoice& Voice : ActiveVoices)
	{
		const UAudioComponent* Comp = Voice.Component.Get();
		if (Comp && Comp->IsPlaying())
		{
			++Count;
		}
	}
	return Count;
}

const FAudio_SoundEntry* UAudio_SoundManagerSubsystem::ResolveEntry(const FGameplayTag& SoundTag) const
{
	for (const TObjectPtr<UAudio_SoundBankDataAsset>& Bank : LoadedBanks)
	{
		if (!Bank)
		{
			continue;
		}
		if (const FAudio_SoundEntry* Entry = Bank->FindEntry(SoundTag))
		{
			return Entry; // First loaded bank wins.
		}
	}
	return nullptr;
}

// =====================================================================================================
// Mix profiles
// =====================================================================================================

FGuid UAudio_SoundManagerSubsystem::PushMixProfile(FGameplayTag MixTag, int32 PriorityOverride)
{
	if (!MixTag.IsValid())
	{
		return FGuid();
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (UAudio_MixProfileDataAsset* Profile = Registry->Find<UAudio_MixProfileDataAsset>(MixTag))
		{
			return PushMixProfileAsset(Profile, PriorityOverride);
		}
	}
	UE_LOG(LogDP, Warning, TEXT("Audio: PushMixProfile could not resolve mix tag '%s'."), *MixTag.ToString());
	return FGuid();
}

FGuid UAudio_SoundManagerSubsystem::PushMixProfileAsset(UAudio_MixProfileDataAsset* Profile, int32 PriorityOverride)
{
	if (!MixController || !Profile)
	{
		return FGuid();
	}
	const FGuid Handle = MixController->PushProfile(Profile, PriorityOverride);

	// A mix change can alter active duck rules; re-scale every live voice's volume for affected
	// categories. Cheap and correct: re-applies base*category*duck across all active voices.
	for (const FAudio_DuckRule& Rule : Profile->DuckRules)
	{
		RefreshCategoryVoiceVolumes(Rule.TargetCategory);
	}
	return Handle;
}

void UAudio_SoundManagerSubsystem::PopMixProfile(FGuid Handle)
{
	if (!MixController || !Handle.IsValid())
	{
		return;
	}
	MixController->PopProfile(Handle);

	// Popping may restore ducked categories; re-scale all live voices so volumes recover. We do not
	// know which categories changed, so refresh every category currently in use.
	TSet<FGameplayTag> Touched;
	for (const FActiveVoice& Voice : ActiveVoices)
	{
		if (Voice.Category.IsValid())
		{
			Touched.Add(Voice.Category);
		}
	}
	for (const FGameplayTag& Cat : Touched)
	{
		RefreshCategoryVoiceVolumes(Cat);
	}
}

// =====================================================================================================
// ADDITIVE deepening — reverb zones + dynamic mixing depth (duck buses)
// =====================================================================================================

FGuid UAudio_SoundManagerSubsystem::PushMixProfileAssetBlended(UAudio_MixProfileDataAsset* Profile, float BlendTimeSeconds, int32 PriorityOverride)
{
	if (!MixController || !Profile)
	{
		return FGuid();
	}
	const FGuid Handle = MixController->PushProfileBlended(Profile, BlendTimeSeconds, PriorityOverride);

	// A mix change can alter active duck rules; re-scale every live voice for affected categories.
	for (const FAudio_DuckRule& Rule : Profile->DuckRules)
	{
		RefreshCategoryVoiceVolumes(Rule.TargetCategory);
	}
	return Handle;
}

FGuid UAudio_SoundManagerSubsystem::PushDuckBus(FGameplayTag DuckBusTag)
{
	if (!DuckBusTag.IsValid())
	{
		return FGuid();
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (UAudio_DuckBusDataAsset* DuckBus = Registry->Find<UAudio_DuckBusDataAsset>(DuckBusTag))
		{
			return PushDuckBusAsset(DuckBus);
		}
	}
	UE_LOG(LogDP, Warning, TEXT("Audio: PushDuckBus could not resolve duck-bus tag '%s'."), *DuckBusTag.ToString());
	return FGuid();
}

FGuid UAudio_SoundManagerSubsystem::PushDuckBusAsset(UAudio_DuckBusDataAsset* DuckBus)
{
	if (!MixController || !DuckBus)
	{
		return FGuid();
	}

	// Build a TRANSIENT mix profile carrying only the duck-bus's duck rules and push it through the
	// existing priority stack. This reuses GetActiveDuckVolume/RefreshCategoryVoiceVolumes verbatim so
	// the duck composes with any other active profile by priority — no parallel ducking engine. The
	// transient profile is kept alive by the controller's snapshot UPROPERTY while it is on the stack.
	UAudio_MixProfileDataAsset* Transient = NewObject<UAudio_MixProfileDataAsset>(this);
	Transient->DataTag = DuckBus->DataTag;
	Transient->Priority = DuckBus->StackPriority;
	Transient->DuckRules = DuckBus->Duckees;

	const FGuid Handle = MixController->PushProfileBlended(Transient, DuckBus->AttackSeconds, DuckBus->StackPriority);

	for (const FAudio_DuckRule& Rule : Transient->DuckRules)
	{
		RefreshCategoryVoiceVolumes(Rule.TargetCategory);
	}

	UE_LOG(LogDP, Verbose, TEXT("Audio: pushed duck bus '%s' (%d duckee(s))."),
		*DuckBus->DataTag.ToString(), DuckBus->Duckees.Num());
	return Handle;
}

void UAudio_SoundManagerSubsystem::ReleaseDuck(FGuid Handle)
{
	// Releasing is identical to popping a mix profile; PopMixProfile already re-scales every category.
	PopMixProfile(Handle);
}

// =====================================================================================================
// Debug
// =====================================================================================================

FString UAudio_SoundManagerSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Audio[device=%s banks=%d voices=%d/%d cats=%d %s]"),
		bAudioAvailable ? TEXT("yes") : TEXT("no"),
		LoadedBanks.Num(),
		GetActiveVoiceCount(),
		ActiveVoices.Num(),
		CategoryVolumes.Num(),
		MixController ? *MixController->GetDebugString() : TEXT("Mix[none]"));
}
