// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Ambience/Audio_AmbienceComponent.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Clock/Seam_SimClock.h"

#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ---------------------------------------------------------------------------------------------
//  FAudio_AmbienceVariant
// ---------------------------------------------------------------------------------------------

bool FAudio_AmbienceVariant::ContainsTime(float NormalizedTime) const
{
	const float T = FMath::Frac(FMath::Max(0.0f, NormalizedTime)); // wrap into [0,1)

	if (FMath::IsNearlyEqual(TimeStart, TimeEnd))
	{
		// Degenerate window treated as "all day" so a single full-range variant always matches.
		return true;
	}

	if (TimeStart <= TimeEnd)
	{
		return T >= TimeStart && T < TimeEnd;
	}

	// Wrapping window (e.g. 0.8 -> 0.2 covers night across midnight).
	return T >= TimeStart || T < TimeEnd;
}

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

UAudio_AmbienceComponent::UAudio_AmbienceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Ambience never needs to tick before the world is ready; group with end-of-frame audio.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UAudio_AmbienceComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveSimClock();

	if (TriggerMode == EAudio_AmbienceTrigger::OnOverlap)
	{
		BindOverlaps();
	}
	else // Always
	{
		bDesiredActive = true;
		PlayVariant(SelectVariantForTime());
	}
}

void UAudio_AmbienceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAudioComponent* Comp = CurrentVoice.Get())
	{
		Comp->Stop();
		Comp->DestroyComponent();
	}
	if (UAudioComponent* Comp = OutgoingVoice.Get())
	{
		Comp->Stop();
		Comp->DestroyComponent();
	}
	CurrentVoice = nullptr;
	OutgoingVoice = nullptr;
	SimClock = nullptr;

	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
//  Clock resolution (soft)
// ---------------------------------------------------------------------------------------------

void UAudio_AmbienceComponent::SetSimClock(const TScriptInterface<ISeam_SimClock>& InClock)
{
	if (UObject* Obj = InClock.GetObject())
	{
		SimClock = TWeakInterfacePtr<ISeam_SimClock>(*Obj);
	}
	else
	{
		SimClock = nullptr;
	}
}

void UAudio_AmbienceComponent::ResolveSimClock()
{
	// Already provided explicitly?
	if (SimClock.IsValid())
	{
		return;
	}

	// 1) Service locator by tag (if the project registered a clock there).
	if (SimClockServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			if (UObject* Provider = Locator->ResolveService(SimClockServiceTag))
			{
				if (Provider->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
				{
					SimClock = TWeakInterfacePtr<ISeam_SimClock>(*Provider);
					return;
				}
			}
		}
	}

	// 2) GameState, then any actor implementing the seam (cheap, one-time scan on BeginPlay).
	if (const UWorld* World = GetWorld())
	{
		if (AGameStateBase* GS = World->GetGameState())
		{
			if (GS->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
			{
				SimClock = TWeakInterfacePtr<ISeam_SimClock>(*GS);
				return;
			}
		}

		for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
			{
				SimClock = TWeakInterfacePtr<ISeam_SimClock>(*Actor);
				return;
			}
		}
	}

	// No clock found — ambience falls back to the first variant. This is fully supported.
	UE_LOG(LogDP, Verbose, TEXT("AmbienceComponent '%s': no SimClock seam found; using first variant only."),
		*GetReadableName());
}

// ---------------------------------------------------------------------------------------------
//  Variant selection
// ---------------------------------------------------------------------------------------------

int32 UAudio_AmbienceComponent::SelectVariantForTime() const
{
	if (Variants.Num() == 0)
	{
		return INDEX_NONE;
	}
	if (Variants.Num() == 1 || !SimClock.IsValid())
	{
		// Single variant, or no clock to consult -> first playable variant.
		for (int32 Index = 0; Index < Variants.Num(); ++Index)
		{
			if (!Variants[Index].Bed.IsNull())
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	// Read time-of-day through the seam (BlueprintNativeEvent -> Execute_ dispatch).
	UObject* ClockObj = SimClock.GetObject();
	const float TimeOfDay = ClockObj ? ISeam_SimClock::Execute_GetNormalizedTimeOfDay(ClockObj) : 0.0f;

	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		if (!Variants[Index].Bed.IsNull() && Variants[Index].ContainsTime(TimeOfDay))
		{
			return Index;
		}
	}

	// No window matched the current time — fall back to the first playable variant.
	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		if (!Variants[Index].Bed.IsNull())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

// ---------------------------------------------------------------------------------------------
//  Playback
// ---------------------------------------------------------------------------------------------

UAudioComponent* UAudio_AmbienceComponent::CreateVoice(USoundBase* Sound) const
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

	UAudioComponent* Comp = UGameplayStatics::CreateSound2D(
		World, Sound,
		/*VolumeMultiplier*/ 1.0f,
		/*PitchMultiplier*/ 1.0f,
		/*StartTime*/ 0.0f,
		/*ConcurrencySettings*/ nullptr,
		/*bPersistAcrossLevelTransition*/ false,
		/*bAutoDestroy*/ false);

	if (Comp)
	{
		Comp->bIsUISound = false;
		Comp->bAllowSpatialization = false; // 2D bed; spatial ambience emitters are a separate concern
	}
	return Comp;
}

void UAudio_AmbienceComponent::PlayVariant(int32 VariantIndex)
{
	if (!Variants.IsValidIndex(VariantIndex))
	{
		return;
	}
	if (VariantIndex == ActiveVariantIndex && CurrentVoice.Get())
	{
		return; // already playing this variant
	}

	USoundBase* Bed = Variants[VariantIndex].Bed.LoadSynchronous();
	if (!Bed)
	{
		UE_LOG(LogDP, Warning, TEXT("AmbienceComponent '%s': variant %d bed failed to load."),
			*GetReadableName(), VariantIndex);
		return;
	}

	// Demote any current voice to outgoing so we crossfade rather than cut.
	if (UAudioComponent* Existing = CurrentVoice.Get())
	{
		if (UAudioComponent* StaleOutgoing = OutgoingVoice.Get())
		{
			StaleOutgoing->Stop();
			StaleOutgoing->DestroyComponent();
		}
		OutgoingVoice = Existing;
		OutgoingFadeAlpha = 1.0f; // start at current level, fade to 0
	}

	UAudioComponent* NewVoice = CreateVoice(Bed);
	if (!NewVoice)
	{
		return;
	}
	NewVoice->SetVolumeMultiplier(0.0f);
	NewVoice->Play(0.0f);

	CurrentVoice = NewVoice;
	ActiveVariantIndex = VariantIndex;
}

// ---------------------------------------------------------------------------------------------
//  Activation
// ---------------------------------------------------------------------------------------------

void UAudio_AmbienceComponent::FadeIn()
{
	bDesiredActive = true;
	if (!CurrentVoice.Get())
	{
		PlayVariant(SelectVariantForTime());
	}
}

void UAudio_AmbienceComponent::FadeOut()
{
	bDesiredActive = false;
}

// ---------------------------------------------------------------------------------------------
//  Overlap gating
// ---------------------------------------------------------------------------------------------

void UAudio_AmbienceComponent::BindOverlaps()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	UPrimitiveComponent* Trigger = Owner->FindComponentByClass<UPrimitiveComponent>();
	if (!Trigger)
	{
		UE_LOG(LogDP, Warning,
			TEXT("AmbienceComponent '%s': OnOverlap mode but owner has no collision primitive."),
			*GetReadableName());
		return;
	}

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &UAudio_AmbienceComponent::HandleBeginOverlap);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &UAudio_AmbienceComponent::HandleEndOverlap);
}

bool UAudio_AmbienceComponent::DoesActorQualify(const AActor* Other) const
{
	const APawn* Pawn = Cast<APawn>(Other);
	if (!Pawn)
	{
		return false;
	}
	if (bOnlyLocalListener)
	{
		// Only the locally-controlled listener pawn should drive local ambience.
		return Pawn->IsLocallyControlled();
	}
	return true;
}

void UAudio_AmbienceComponent::HandleBeginOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*Sweep*/)
{
	if (!DoesActorQualify(OtherActor))
	{
		return;
	}
	++OverlapRefCount;
	if (OverlapRefCount == 1)
	{
		FadeIn();
	}
}

void UAudio_AmbienceComponent::HandleEndOverlap(
	UPrimitiveComponent* /*OverlappedComp*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/,
	int32 /*OtherBodyIndex*/)
{
	if (!DoesActorQualify(OtherActor))
	{
		return;
	}
	OverlapRefCount = FMath::Max(0, OverlapRefCount - 1);
	if (OverlapRefCount == 0)
	{
		FadeOut();
	}
}

// ---------------------------------------------------------------------------------------------
//  Tick
// ---------------------------------------------------------------------------------------------

void UAudio_AmbienceComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 1) Drive the master fade toward the desired state.
	const float TargetAlpha = bDesiredActive ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(FadeAlpha, TargetAlpha))
	{
		const float Duration = bDesiredActive ? FadeInSeconds : FadeOutSeconds;
		if (Duration <= KINDA_SMALL_NUMBER)
		{
			FadeAlpha = TargetAlpha;
		}
		else
		{
			const float Step = DeltaTime / Duration;
			FadeAlpha = (TargetAlpha > FadeAlpha)
				? FMath::Min(FadeAlpha + Step, TargetAlpha)
				: FMath::Max(FadeAlpha - Step, TargetAlpha);
		}
	}

	// 2) When fully active, re-evaluate the time-of-day variant and crossfade if it changed.
	if (bDesiredActive && FadeAlpha > 0.0f)
	{
		const int32 Desired = SelectVariantForTime();
		if (Desired != INDEX_NONE && Desired != ActiveVariantIndex)
		{
			PlayVariant(Desired);
		}
	}

	// 3) Apply volumes. Current voice = master fade * variant volume * component volume.
	if (UAudioComponent* Comp = CurrentVoice.Get())
	{
		const float VariantVol = Variants.IsValidIndex(ActiveVariantIndex) ? Variants[ActiveVariantIndex].Volume : 1.0f;
		const float Target = FadeAlpha * AmbienceVolume * VariantVol;
		Comp->SetVolumeMultiplier(FMath::Max(0.0f, Target));

		// Fully faded out and not desired -> stop and release the voice.
		if (!bDesiredActive && FadeAlpha <= KINDA_SMALL_NUMBER)
		{
			Comp->Stop();
			Comp->DestroyComponent();
			CurrentVoice = nullptr;
			ActiveVariantIndex = INDEX_NONE;
		}
	}

	// 4) Drive the outgoing (variant-crossfade) voice to silence, then release it.
	if (UAudioComponent* Out = OutgoingVoice.Get())
	{
		if (VariantCrossfadeSeconds <= KINDA_SMALL_NUMBER)
		{
			OutgoingFadeAlpha = 0.0f;
		}
		else
		{
			OutgoingFadeAlpha = FMath::Max(0.0f, OutgoingFadeAlpha - (DeltaTime / VariantCrossfadeSeconds));
		}

		const float Target = OutgoingFadeAlpha * FadeAlpha * AmbienceVolume;
		Out->SetVolumeMultiplier(FMath::Max(0.0f, Target));

		if (OutgoingFadeAlpha <= KINDA_SMALL_NUMBER)
		{
			Out->Stop();
			Out->DestroyComponent();
			OutgoingVoice = nullptr;
		}
	}
}
