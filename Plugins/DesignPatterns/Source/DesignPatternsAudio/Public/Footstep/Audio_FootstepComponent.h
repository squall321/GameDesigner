// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Engine/EngineTypes.h"
#include "Audio_FootstepComponent.generated.h"

class UAudio_SurfaceBankDataAsset;
class IAudio_AudioController;

/**
 * FOOTSTEP/SURFACE (5) component placed on a walking character.
 *
 * On an anim-notify (or any external call) it determines the surface under a foot bone — either from a
 * supplied hit, or by tracing straight down from the bone — reads the EPhysicalSurface via the engine
 * physical-material API, maps (surface, gait) to a footstep sound tag through a UAudio_SurfaceBankDataAsset,
 * and plays it spatialized through the IAudio_AudioController seam (resolved from DP.Service.Audio).
 *
 * Purely LOCAL / COSMETIC and never replicated: every client plays its own footsteps from its own
 * animation. No genre coupling — it only knows tags, the surface bank and the audio seam.
 */
UCLASS(ClassGroup = (DesignPatternsAudio), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAUDIO_API UAudio_FootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAudio_FootstepComponent();

	/**
	 * Play a footstep for FootBone using the current Gait. Traces down from the bone (or the owner if
	 * the bone is not found) to find the surface. The usual entry point from an anim notify.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|Footstep")
	void PlayFootstep(FName FootBone, FGameplayTag Gait);

	/** Play a footstep from an already-known hit (e.g. a movement component's floor result). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|Footstep")
	void PlayFootstepAtHit(const FHitResult& Hit, FGameplayTag Gait);

	/** Set the gait used by PlayFootstep when none is passed (walk/run/sprint...). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|Footstep")
	void SetGait(FGameplayTag InGait) { CurrentGait = InGait; }

protected:
	/** The surface->footstep bank this character uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	TObjectPtr<UAudio_SurfaceBankDataAsset> SurfaceBank;

	/** Linear volume multiplier applied to every footstep this component plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float FootstepVolumeMult = 1.f;

	/** Distance (cm) to trace down from a foot bone to find the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (ClampMin = "1.0", UIMax = "500.0", Units = "cm"))
	float TraceDownDistance = 120.f;

	/** Collision channel used for the foot trace (should hit floors with physical materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Default gait used by PlayFootstep when the caller passes an invalid gait tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (Categories = "DP.Audio.Surface"))
	FGameplayTag CurrentGait;

private:
	/** Resolve and play a footstep sound for a known surface at a location. */
	void PlayResolved(EPhysicalSurface Surface, const FVector& Location, FGameplayTag Gait);

	/** Resolve the IAudio_AudioController seam from the service locator. Null if unavailable. */
	IAudio_AudioController* ResolveAudioController() const;

	/** Find a foot bone world location on the owner's first skeletal mesh; falls back to owner location. */
	FVector GetFootWorldLocation(FName FootBone) const;
};
