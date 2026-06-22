// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Audio_OcclusionComponent.generated.h"

class UAudioComponent;

/**
 * Per-source override of the project occlusion tunables. Optional; when a field is < 0 it inherits the
 * UAudio_OcclusionSettings value. Lets an individual emitter be more/less muffled than the global
 * default (e.g. a generator that stays audible through walls) without code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_OcclusionParams
{
	GENERATED_BODY()

	/** Low-pass cutoff (Hz) at full occlusion. < 0 = inherit settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion", meta = (ClampMin = "-1.0", UIMax = "20000.0"))
	float OccludedLowPassHz = -1.f;

	/** Linear volume multiplier at full occlusion. < 0 = inherit settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float OccludedVolumeMult = -1.f;

	/** Easing speed (per second) toward the target occlusion factor. < 0 = inherit settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion", meta = (ClampMin = "-1.0", UIMax = "20.0"))
	float InterpSpeed = -1.f;

	/** Local-space offset from the owner used as the source point for the occlusion trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion")
	FVector SourceOffset = FVector::ZeroVector;
};

/**
 * OCCLUSION (1). Per-source occlusion driver for persistent looping voices on its owner.
 *
 * Register the looping UAudioComponents that should be occluded (RegisterVoice). The component
 * self-registers into the world's UAudio_OcclusionService on BeginPlay; the service round-robin
 * line-traces from the audio listener(s) to this source and pushes a target occlusion factor [0,1]
 * (0 = clear line of sight, 1 = fully blocked) via SetTargetOcclusion. Each frame the component eases
 * its current factor toward the target and applies the resulting LOW-PASS cutoff and volume
 * MULTIPLIER to every tracked voice — wrapping UAudioComponent::SetLowPassFilterEnabled /
 * SetLowPassFilterFrequency / SetVolumeMultiplier (never a hand-rolled DSP).
 *
 * Purely LOCAL / COSMETIC and never replicated: occlusion is geometry-driven and evaluated per client.
 * Self-throttling: when no voices are tracked, or audio is unavailable, it is a guarded no-op. Voices
 * are held WEAKLY (matching the sound manager's FActiveVoice convention) so a finished/auto-destroyed
 * component is simply dropped.
 */
UCLASS(ClassGroup = (DesignPatternsAudio), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAUDIO_API UAudio_OcclusionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAudio_OcclusionComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Per-source occlusion tuning (overrides the project settings where a field is >= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion")
	FAudio_OcclusionParams Params;

	/** Start tracking a looping voice so its volume/low-pass follows this source's occlusion. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void RegisterVoice(UAudioComponent* Voice);

	/** Stop tracking a voice (its filter/volume are left as-is; it will be GC'd when it finishes). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void UnregisterVoice(UAudioComponent* Voice);

	/** Enable/disable occlusion for this source. When disabled it eases back to fully un-occluded. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void SetOcclusionEnabled(bool bEnabled);

	/** The eased current occlusion factor [0,1] (0 = clear, 1 = fully occluded). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Audio")
	float GetCurrentOcclusion() const { return CurrentOcclusion; }

	/**
	 * Set the TARGET occlusion factor [0,1] for this source. Called by the occlusion service after a
	 * trace; the component eases CurrentOcclusion toward this each tick. (Also callable directly for a
	 * gameplay-driven occlusion, e.g. a closing blast door, without the trace.)
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void SetTargetOcclusion(float InTarget01);

	/** World-space point used as the trace target for this source (owner location + SourceOffset). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Audio")
	FVector GetSourceWorldLocation() const;

	/** True while this source has at least one live tracked voice (cheap gate for the service). */
	bool HasLiveVoices() const;

private:
	/** Tracked looping voices, WEAK like the sound manager's FActiveVoice. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UAudioComponent>> TrackedVoices;

	/** Target occlusion factor [0,1] from the latest trace / gameplay drive. */
	float TargetOcclusion = 0.f;

	/** Eased current occlusion factor [0,1] applied to voices. */
	float CurrentOcclusion = 0.f;

	/** When false the source eases to 0 (un-occluded) and stops contributing. */
	bool bOcclusionEnabled = true;

	/** True once registered with the world occlusion service (so EndPlay unregisters exactly once). */
	bool bRegisteredWithService = false;

	/** Drop tracked voices whose component finished / was GC'd. */
	void PruneVoices();

	/** Apply CurrentOcclusion's LPF + volume multiplier to every live tracked voice. */
	void ApplyToVoices();
};
