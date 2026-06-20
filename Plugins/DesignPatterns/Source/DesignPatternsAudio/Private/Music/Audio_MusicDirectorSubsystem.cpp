// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Music/Audio_MusicDirectorSubsystem.h"
#include "Music/Audio_MusicStateDataAsset.h"
#include "Music/Audio_MusicEventMapDataAsset.h"
#include "Music/Audio_MusicDirectorSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------------
//  FAudio_MusicVoice
// ---------------------------------------------------------------------------------------------

void FAudio_MusicVoice::StartFade(float NewTarget, float Duration)
{
	StartVolume = CurrentVolume;
	TargetVolume = FMath::Max(0.0f, NewTarget);
	FadeDuration = FMath::Max(0.0f, Duration);
	FadeElapsed = 0.0f;

	// Snap immediately when there is no fade window.
	if (FadeDuration <= KINDA_SMALL_NUMBER)
	{
		CurrentVolume = TargetVolume;
	}
}

bool FAudio_MusicVoice::Advance(float DeltaTime)
{
	if (FadeDuration <= KINDA_SMALL_NUMBER)
	{
		CurrentVolume = TargetVolume;
		return true;
	}

	FadeElapsed += DeltaTime;
	const float Alpha = FMath::Clamp(FadeElapsed / FadeDuration, 0.0f, 1.0f);
	CurrentVolume = FMath::Lerp(StartVolume, TargetVolume, Alpha);
	return Alpha >= 1.0f;
}

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UAudio_MusicDirectorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Music is meaningless without a sound device; skip dedicated-server creation entirely.
	return !IsRunningDedicatedServer();
}

void UAudio_MusicDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Drive volume crossfades and intensity easing off the core ticker (we are not FTickable).
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAudio_MusicDirectorSubsystem::Tick));

	// Install the project default event map (async-soft load to a sync resolve on use).
	const UAudio_MusicDirectorSettings& Settings = UAudio_MusicDirectorSettings::GetChecked();
	if (!Settings.DefaultEventMap.IsNull())
	{
		if (UAudio_MusicEventMapDataAsset* Map = Settings.DefaultEventMap.LoadSynchronous())
		{
			InstallEventMap(Map);
		}
		else
		{
			UE_LOG(LogDP, Warning,
				TEXT("MusicDirector: configured DefaultEventMap '%s' failed to load."),
				*Settings.DefaultEventMap.ToString());
		}
	}

	UE_LOG(LogDP, Log, TEXT("MusicDirector initialized."));
}

void UAudio_MusicDirectorSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	ClearBusSubscriptions();

	// Stop and release every live voice so no audio leaks past teardown.
	for (FAudio_MusicVoice& Voice : Voices)
	{
		ReleaseVoice(Voice);
	}
	Voices.Reset();

	ActiveState = nullptr;
	ActiveStateTag = FGameplayTag();
	EventMap.Reset();

	UE_LOG(LogDP, Log, TEXT("MusicDirector deinitialized."));
	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
//  Event map / bus subscriptions
// ---------------------------------------------------------------------------------------------

void UAudio_MusicDirectorSubsystem::InstallEventMap(UAudio_MusicEventMapDataAsset* InEventMap)
{
	ClearBusSubscriptions();
	EventMap = InEventMap;

	if (!InEventMap)
	{
		UE_LOG(LogDP, Log, TEXT("MusicDirector: event map cleared."));
		return;
	}

	RefreshBusSubscriptions();

	// Optionally enter the map's declared default state.
	if (InEventMap->DefaultStateTag.IsValid())
	{
		SetMusicState(InEventMap->DefaultStateTag);
	}

	UE_LOG(LogDP, Log, TEXT("MusicDirector: installed event map '%s' (%d rules)."),
		*InEventMap->GetName(), InEventMap->Rules.Num());
}

void UAudio_MusicDirectorSubsystem::RefreshBusSubscriptions()
{
	ClearBusSubscriptions();

	UAudio_MusicEventMapDataAsset* Map = EventMap.Get();
	if (!Map)
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Warning, TEXT("MusicDirector: message bus unavailable; reactions disabled."));
		return;
	}

	TArray<FGameplayTag> Channels;
	Map->GetDistinctChannels(Channels);

	for (const FGameplayTag& Channel : Channels)
	{
		// ExactOrChild so a rule on DP.Bus.Combat catches DP.Bus.Combat.Begin etc.
		TWeakObjectPtr<UAudio_MusicDirectorSubsystem> WeakThis(this);
		FDP_ListenerHandle Handle = Bus->ListenNative(
			Channel,
			[WeakThis](const FDP_Message& Message)
			{
				if (UAudio_MusicDirectorSubsystem* Self = WeakThis.Get())
				{
					Self->HandleBusMessage(Message);
				}
			},
			this,
			EDP_MessageMatch::ExactOrChild);

		BusListenerHandles.Add(Handle);
	}

	UE_LOG(LogDP, Verbose, TEXT("MusicDirector: subscribed to %d bus channel(s)."), Channels.Num());
}

void UAudio_MusicDirectorSubsystem::ClearBusSubscriptions()
{
	if (BusListenerHandles.Num() == 0)
	{
		return;
	}

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// One owner-scoped removal cleans up every listener we registered with `this` as owner.
		Bus->StopListeningForOwner(this);
	}
	BusListenerHandles.Reset();
}

void UAudio_MusicDirectorSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	UAudio_MusicEventMapDataAsset* Map = EventMap.Get();
	if (!Map)
	{
		return;
	}

	TArray<FAudio_MusicEventRule> Matching;
	Map->GatherMatchingRules(Message.Channel, /*bAllowChildMatch*/ true, Matching);

	for (const FAudio_MusicEventRule& Rule : Matching)
	{
		ApplyRule(Rule);
	}
}

void UAudio_MusicDirectorSubsystem::ApplyRule(const FAudio_MusicEventRule& Rule)
{
	switch (Rule.Action)
	{
	case EAudio_MusicEventAction::SetState:
		SetMusicState(Rule.TargetStateTag);
		break;

	case EAudio_MusicEventAction::TriggerStinger:
		TriggerStinger(Rule.StingerTag);
		break;

	case EAudio_MusicEventAction::SetIntensity:
		SetIntensity(Rule.IntensityValue);
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------
//  State resolution
// ---------------------------------------------------------------------------------------------

UAudio_MusicStateDataAsset* UAudio_MusicDirectorSubsystem::ResolveState(FGameplayTag StateTag) const
{
	if (!StateTag.IsValid())
	{
		return nullptr;
	}

	// 1) Prefer the installed map's explicit playlist (self-contained music set).
	if (UAudio_MusicEventMapDataAsset* Map = EventMap.Get())
	{
		for (const TSoftObjectPtr<UAudio_MusicStateDataAsset>& SoftState : Map->Playlist)
		{
			if (SoftState.IsNull())
			{
				continue;
			}
			if (UAudio_MusicStateDataAsset* State = SoftState.LoadSynchronous())
			{
				if (State->DataTag == StateTag)
				{
					return State;
				}
			}
		}
	}

	// 2) Fall back to the core data registry (tag-keyed).
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (UAudio_MusicStateDataAsset* State = Registry->Find<UAudio_MusicStateDataAsset>(StateTag))
		{
			return State;
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------------------------
//  Core music API
// ---------------------------------------------------------------------------------------------

void UAudio_MusicDirectorSubsystem::SetMusicState(FGameplayTag StateTag)
{
	if (!StateTag.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("MusicDirector: SetMusicState called with invalid tag; ignored."));
		return;
	}
	if (StateTag == ActiveStateTag)
	{
		return; // already there
	}

	UAudio_MusicStateDataAsset* State = ResolveState(StateTag);
	if (!State)
	{
		UE_LOG(LogDP, Warning, TEXT("MusicDirector: SetMusicState could not resolve state tag '%s'."),
			*StateTag.ToString());
		return;
	}

	SetMusicStateAsset(State);
}

void UAudio_MusicDirectorSubsystem::SetMusicStateAsset(UAudio_MusicStateDataAsset* State)
{
	if (!State)
	{
		return;
	}
	if (State == ActiveState)
	{
		return;
	}

	const float Crossfade = (State->CrossfadeSeconds >= 0.0f)
		? State->CrossfadeSeconds
		: UAudio_MusicDirectorSettings::GetChecked().FallbackCrossfadeSeconds;

	// Fade the outgoing state's voices out and bring the new state's layers in over the same window.
	RetireActiveVoices(Crossfade);

	ActiveState = State;
	ActiveStateTag = State->DataTag;

	SpawnVoicesForActiveState(Crossfade);

	UE_LOG(LogDP, Verbose, TEXT("MusicDirector: -> state '%s' (crossfade %.2fs)."),
		*ActiveStateTag.ToString(), Crossfade);
}

void UAudio_MusicDirectorSubsystem::StopMusic(float FadeSeconds)
{
	RetireActiveVoices(FMath::Max(0.0f, FadeSeconds));
	ActiveState = nullptr;
	ActiveStateTag = FGameplayTag();
	UE_LOG(LogDP, Verbose, TEXT("MusicDirector: StopMusic (fade %.2fs)."), FadeSeconds);
}

void UAudio_MusicDirectorSubsystem::TriggerStinger(FGameplayTag StingerTag)
{
	if (!StingerTag.IsValid() || !ActiveState)
	{
		return;
	}

	const TSoftObjectPtr<USoundBase> SoftStinger = ActiveState->FindStinger(StingerTag);
	if (SoftStinger.IsNull())
	{
		UE_LOG(LogDP, Verbose, TEXT("MusicDirector: state '%s' has no stinger '%s'."),
			*ActiveStateTag.ToString(), *StingerTag.ToString());
		return;
	}

	USoundBase* Stinger = SoftStinger.LoadSynchronous();
	if (!Stinger)
	{
		return;
	}

	const float Master = UAudio_MusicDirectorSettings::GetChecked().MasterMusicVolume;

	// Fire-and-forget 2D one-shot over the running bed; does not touch the looping layer voices.
	UGameplayStatics::PlaySound2D(this, Stinger, Master);

	UE_LOG(LogDP, Verbose, TEXT("MusicDirector: stinger '%s'."), *StingerTag.ToString());
}

void UAudio_MusicDirectorSubsystem::SetIntensity(float NewIntensity)
{
	TargetIntensity = FMath::Clamp(NewIntensity, 0.0f, 1.0f);

	// If interpolation is disabled, snap and remix now so the change is immediate.
	if (UAudio_MusicDirectorSettings::GetChecked().IntensityInterpSpeed <= 0.0f)
	{
		CurrentIntensity = TargetIntensity;
		RemixActiveVoices();
	}
}

// ---------------------------------------------------------------------------------------------
//  Voice management
// ---------------------------------------------------------------------------------------------

void UAudio_MusicDirectorSubsystem::RetireActiveVoices(float Duration)
{
	for (FAudio_MusicVoice& Voice : Voices)
	{
		if (Voice.LayerIndex != INDEX_NONE && !Voice.bRetiring)
		{
			Voice.bRetiring = true;
			Voice.LayerIndex = INDEX_NONE; // detach from the (about to change) active state
			Voice.StartFade(0.0f, Duration);
		}
	}
}

void UAudio_MusicDirectorSubsystem::SpawnVoicesForActiveState(float Duration)
{
	if (!ActiveState)
	{
		return;
	}

	const float Master = UAudio_MusicDirectorSettings::GetChecked().MasterMusicVolume;

	for (int32 Index = 0; Index < ActiveState->Layers.Num(); ++Index)
	{
		const FAudio_MusicLayer& Layer = ActiveState->Layers[Index];
		if (Layer.Stem.IsNull())
		{
			continue;
		}

		USoundBase* Stem = Layer.Stem.LoadSynchronous();
		if (!Stem)
		{
			UE_LOG(LogDP, Warning, TEXT("MusicDirector: layer %d stem failed to load for state '%s'."),
				Index, *ActiveStateTag.ToString());
			continue;
		}

		UAudioComponent* Comp = AcquireVoiceComponent(Stem);
		if (!Comp)
		{
			continue;
		}

		// All layers of a state start together so they stay phase-locked.
		Comp->SetVolumeMultiplier(0.0f);
		Comp->Play(0.0f);

		FAudio_MusicVoice NewVoice;
		NewVoice.Component = Comp;
		NewVoice.LayerIndex = Index;
		NewVoice.CurrentVolume = 0.0f;

		// Fade in to the layer's current intensity-driven target.
		const float Target = Master * ActiveState->StateVolume * Layer.ComputeTargetVolume(CurrentIntensity);
		NewVoice.StartFade(Target, Duration);

		Voices.Add(MoveTemp(NewVoice));
	}
}

void UAudio_MusicDirectorSubsystem::RemixActiveVoices()
{
	if (!ActiveState)
	{
		return;
	}

	const float Master = UAudio_MusicDirectorSettings::GetChecked().MasterMusicVolume;

	for (FAudio_MusicVoice& Voice : Voices)
	{
		if (Voice.LayerIndex == INDEX_NONE || Voice.bRetiring)
		{
			continue;
		}
		if (!ActiveState->Layers.IsValidIndex(Voice.LayerIndex))
		{
			continue;
		}

		const FAudio_MusicLayer& Layer = ActiveState->Layers[Voice.LayerIndex];
		const float Target = Master * ActiveState->StateVolume * Layer.ComputeTargetVolume(CurrentIntensity);

		// Snap the target without restarting the fade window — intensity easing is handled by Tick.
		Voice.TargetVolume = Target;
		Voice.StartVolume = Voice.CurrentVolume;
		Voice.FadeElapsed = 0.0f;
		Voice.FadeDuration = 0.0f; // intensity remixes track instantly; the Tick smooths intensity itself
		Voice.CurrentVolume = Target;
	}
}

UAudioComponent* UAudio_MusicDirectorSubsystem::AcquireVoiceComponent(USoundBase* Sound)
{
	if (!Sound)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// CreateSound2D builds a persistent, non-auto-destroy component we drive manually.
	UAudioComponent* Comp = UGameplayStatics::CreateSound2D(
		World, Sound,
		/*VolumeMultiplier*/ 1.0f,
		/*PitchMultiplier*/ 1.0f,
		/*StartTime*/ 0.0f,
		/*ConcurrencySettings*/ nullptr,
		/*bPersistAcrossLevelTransition*/ true,
		/*bAutoDestroy*/ false);

	if (!Comp)
	{
		return nullptr;
	}

	Comp->bIsUISound = true;            // not affected by world pause / spatialization
	Comp->bAllowSpatialization = false; // 2D music bed

	// Route to the configured music sound class when present (lets the mix model attach a bus).
	const UAudio_MusicDirectorSettings& Settings = UAudio_MusicDirectorSettings::GetChecked();
	if (!Settings.MusicSoundClass.IsNull())
	{
		if (USoundClass* MusicClass = Settings.MusicSoundClass.LoadSynchronous())
		{
			Comp->SoundClassOverride = MusicClass;
		}
	}

	return Comp;
}

void UAudio_MusicDirectorSubsystem::ReleaseVoice(FAudio_MusicVoice& Voice)
{
	if (UAudioComponent* Comp = Voice.Component.Get())
	{
		Comp->Stop();
		Comp->DestroyComponent();
	}
	Voice.Component = nullptr;
	Voice.LayerIndex = INDEX_NONE;
	Voice.bRetiring = false;
	Voice.CurrentVolume = 0.0f;
	Voice.TargetVolume = 0.0f;
}

// ---------------------------------------------------------------------------------------------
//  Tick
// ---------------------------------------------------------------------------------------------

bool UAudio_MusicDirectorSubsystem::Tick(float DeltaTime)
{
	// 1) Ease intensity toward its target, then re-target active-layer volumes if it moved.
	const UAudio_MusicDirectorSettings& Settings = UAudio_MusicDirectorSettings::GetChecked();
	const float InterpSpeed = Settings.IntensityInterpSpeed;
	if (!FMath::IsNearlyEqual(CurrentIntensity, TargetIntensity))
	{
		if (InterpSpeed > 0.0f)
		{
			CurrentIntensity = FMath::FInterpTo(CurrentIntensity, TargetIntensity, DeltaTime, InterpSpeed);
		}
		else
		{
			CurrentIntensity = TargetIntensity;
		}

		// Re-target active voices toward the new intensity without restarting their crossfade window.
		if (ActiveState)
		{
			const float Master = Settings.MasterMusicVolume;
			for (FAudio_MusicVoice& Voice : Voices)
			{
				if (Voice.LayerIndex == INDEX_NONE || Voice.bRetiring)
				{
					continue;
				}
				if (!ActiveState->Layers.IsValidIndex(Voice.LayerIndex))
				{
					continue;
				}
				const FAudio_MusicLayer& Layer = ActiveState->Layers[Voice.LayerIndex];
				Voice.TargetVolume = Master * ActiveState->StateVolume * Layer.ComputeTargetVolume(CurrentIntensity);
				// Keep any in-flight crossfade duration; the lerp now aims at the updated target.
				if (Voice.FadeDuration <= KINDA_SMALL_NUMBER)
				{
					Voice.CurrentVolume = Voice.TargetVolume;
				}
			}
		}
	}

	// 2) Advance each voice's fade and push its current volume to the engine component.
	for (int32 Index = Voices.Num() - 1; Index >= 0; --Index)
	{
		FAudio_MusicVoice& Voice = Voices[Index];

		const bool bFinished = Voice.Advance(DeltaTime);

		if (UAudioComponent* Comp = Voice.Component.Get())
		{
			Comp->SetVolumeMultiplier(FMath::Max(0.0f, Voice.CurrentVolume));
		}
		else
		{
			// Component was GC'd / destroyed elsewhere — drop the bookkeeping slot.
			Voices.RemoveAtSwap(Index);
			continue;
		}

		// Retire silent fading-out voices.
		if (Voice.bRetiring && bFinished && Voice.CurrentVolume <= KINDA_SMALL_NUMBER)
		{
			ReleaseVoice(Voice);
			Voices.RemoveAtSwap(Index);
		}
	}

	return true; // keep ticking
}

// ---------------------------------------------------------------------------------------------
//  Debug
// ---------------------------------------------------------------------------------------------

FString UAudio_MusicDirectorSubsystem::GetDPDebugString_Implementation() const
{
	int32 ActiveVoices = 0;
	int32 RetiringVoices = 0;
	for (const FAudio_MusicVoice& Voice : Voices)
	{
		if (Voice.bRetiring)
		{
			++RetiringVoices;
		}
		else if (Voice.LayerIndex != INDEX_NONE)
		{
			++ActiveVoices;
		}
	}

	return FString::Printf(
		TEXT("MusicDirector | State: %s | Intensity: %.2f->%.2f | Voices: %d active, %d retiring | Map: %s"),
		ActiveStateTag.IsValid() ? *ActiveStateTag.ToString() : TEXT("<none>"),
		CurrentIntensity, TargetIntensity,
		ActiveVoices, RetiringVoices,
		EventMap.IsValid() ? *EventMap->GetName() : TEXT("<none>"));
}
