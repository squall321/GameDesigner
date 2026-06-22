// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Audio_FootstepLibrary.generated.h"

class UAudio_SurfaceBankDataAsset;

/**
 * FOOTSTEP/SURFACE (5) helpers so Blueprints / anim graphs can fire footsteps without a component.
 *
 * GetSurfaceFromHit wraps UGameplayStatics::GetSurfaceType (the engine physical-material surface
 * lookup). PlaySurfaceFootstep resolves the sound tag from a surface bank and plays it through the
 * IAudio_AudioController seam resolved from the service locator — never a hand-rolled play path and
 * never a hard include of the concrete sound manager.
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_FootstepLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Read the EPhysicalSurface from a hit's physical material (engine wrapper). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Audio|Footstep")
	static EPhysicalSurface GetSurfaceFromHit(const FHitResult& Hit);

	/**
	 * Resolve a footstep sound tag from Bank for (Surface, Gait) and play it spatialized at Location
	 * via the IAudio_AudioController seam. No-op (safe) when the controller/bank is missing or nothing
	 * maps. WorldContext resolves the GameInstance the audio service is registered in.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|Footstep", meta = (WorldContext = "WorldContextObject"))
	static void PlaySurfaceFootstep(UObject* WorldContextObject, FVector Location, UAudio_SurfaceBankDataAsset* Bank,
		FGameplayTag Gait, float VolumeMult = 1.f);

	/**
	 * Convenience: trace straight DOWN from Origin by TraceDownDistance against the given channel and
	 * play the footstep for whatever surface is hit. Returns false if nothing was hit / nothing mapped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|Footstep", meta = (WorldContext = "WorldContextObject"))
	static bool PlayFootstepTraceDown(UObject* WorldContextObject, FVector Origin, float TraceDownDistance,
		TEnumAsByte<ECollisionChannel> TraceChannel, UAudio_SurfaceBankDataAsset* Bank, FGameplayTag Gait,
		float VolumeMult = 1.f);
};
