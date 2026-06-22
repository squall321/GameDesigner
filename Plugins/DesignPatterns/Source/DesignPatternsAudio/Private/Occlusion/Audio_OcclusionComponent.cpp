// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Occlusion/Audio_OcclusionComponent.h"
#include "Occlusion/Audio_OcclusionService.h"
#include "Occlusion/Audio_OcclusionSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"

UAudio_OcclusionComponent::UAudio_OcclusionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Occlusion easing is cosmetic; tick a little coarser than render to save cost.
	PrimaryComponentTick.TickInterval = 0.f; // every frame; cheap (no traces here, just lerp + apply)
}

void UAudio_OcclusionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UAudio_OcclusionService* Service = FDP_SubsystemStatics::GetWorldSubsystem<UAudio_OcclusionService>(this))
	{
		Service->RegisterSource(this);
		bRegisteredWithService = true;
	}
}

void UAudio_OcclusionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegisteredWithService)
	{
		if (UAudio_OcclusionService* Service = FDP_SubsystemStatics::GetWorldSubsystem<UAudio_OcclusionService>(this))
		{
			Service->UnregisterSource(this);
		}
		bRegisteredWithService = false;
	}

	TrackedVoices.Reset();
	Super::EndPlay(EndPlayReason);
}

void UAudio_OcclusionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	PruneVoices();
	if (TrackedVoices.Num() == 0)
	{
		return; // Nothing to drive — self-throttle.
	}

	const UAudio_OcclusionSettings* Settings = UAudio_OcclusionSettings::Get();
	// Defensive fallback easing speed if the CDO is somehow null.
	float InterpSpeed = (Params.InterpSpeed >= 0.f)
		? Params.InterpSpeed
		: (Settings ? Settings->InterpSpeed : 6.f);

	// When disabled, ease the source open (toward 0) so it never stays muffled.
	const float EffectiveTarget = bOcclusionEnabled ? TargetOcclusion : 0.f;

	if (InterpSpeed > 0.f)
	{
		CurrentOcclusion = FMath::FInterpTo(CurrentOcclusion, EffectiveTarget, DeltaTime, InterpSpeed);
	}
	else
	{
		CurrentOcclusion = EffectiveTarget;
	}
	CurrentOcclusion = FMath::Clamp(CurrentOcclusion, 0.f, 1.f);

	ApplyToVoices();
}

void UAudio_OcclusionComponent::RegisterVoice(UAudioComponent* Voice)
{
	if (!Voice)
	{
		return;
	}
	for (const TWeakObjectPtr<UAudioComponent>& Existing : TrackedVoices)
	{
		if (Existing.Get() == Voice)
		{
			return; // Already tracked.
		}
	}
	TrackedVoices.Add(Voice);
}

void UAudio_OcclusionComponent::UnregisterVoice(UAudioComponent* Voice)
{
	TrackedVoices.RemoveAll([Voice](const TWeakObjectPtr<UAudioComponent>& V) { return V.Get() == Voice; });
}

void UAudio_OcclusionComponent::SetOcclusionEnabled(bool bEnabled)
{
	bOcclusionEnabled = bEnabled;
}

void UAudio_OcclusionComponent::SetTargetOcclusion(float InTarget01)
{
	TargetOcclusion = FMath::Clamp(InTarget01, 0.f, 1.f);
}

FVector UAudio_OcclusionComponent::GetSourceWorldLocation() const
{
	const AActor* Owner = GetOwner();
	const FVector Base = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	const FVector OffsetWS = Owner ? Owner->GetActorTransform().TransformVectorNoScale(Params.SourceOffset) : Params.SourceOffset;
	return Base + OffsetWS;
}

bool UAudio_OcclusionComponent::HasLiveVoices() const
{
	for (const TWeakObjectPtr<UAudioComponent>& V : TrackedVoices)
	{
		if (V.IsValid())
		{
			return true;
		}
	}
	return false;
}

void UAudio_OcclusionComponent::PruneVoices()
{
	TrackedVoices.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& V) { return !V.IsValid(); });
}

void UAudio_OcclusionComponent::ApplyToVoices()
{
	const UAudio_OcclusionSettings* Settings = UAudio_OcclusionSettings::Get();

	// Resolve effective targets, honouring per-source overrides then settings then defensive fallbacks.
	const float FullLpfHz = (Params.OccludedLowPassHz >= 0.f)
		? Params.OccludedLowPassHz
		: (Settings ? Settings->OccludedLowPassHz : 600.f);
	const float FullVolMult = (Params.OccludedVolumeMult >= 0.f)
		? Params.OccludedVolumeMult
		: (Settings ? Settings->OccludedVolumeMult : 0.5f);

	// Open (un-occluded) reference points: 20 kHz LPF (effectively bypass) and unity volume.
	constexpr float OpenLpfHz = 20000.f;
	const float Cutoff = FMath::Lerp(OpenLpfHz, FullLpfHz, CurrentOcclusion);
	const float VolMult = FMath::Lerp(1.f, FullVolMult, CurrentOcclusion);

	const bool bEngageLpf = CurrentOcclusion > KINDA_SMALL_NUMBER;

	for (const TWeakObjectPtr<UAudioComponent>& Weak : TrackedVoices)
	{
		UAudioComponent* Comp = Weak.Get();
		if (!Comp)
		{
			continue;
		}
		// WRAP the engine voice API; never a hand-rolled DSP filter.
		Comp->SetLowPassFilterEnabled(bEngageLpf);
		if (bEngageLpf)
		{
			Comp->SetLowPassFilterFrequency(Cutoff);
		}
		// NOTE: this assumes the voice's base volume is unity for the occlusion stage. Sources that need
		// to compose occlusion with a category trim should route through the sound manager instead; the
		// occlusion component is for self-managed looping emitters.
		Comp->SetVolumeMultiplier(VolMult);
	}
}
