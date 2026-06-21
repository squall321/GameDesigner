// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_VfxController.generated.h"

/** Opaque handle to a spawned VFX instance (for stopping an attached/looping effect). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_VfxHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Vfx")
	int64 Id = 0;

	bool IsValid() const { return Id != 0; }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_VfxController : public UInterface
{
	GENERATED_BODY()
};

/**
 * Tag-keyed cosmetic VFX seam (mirrors the audio controller seam). Implemented by the WorldSystems VFX
 * manager; HUD, interaction, narrative, camera and weather request effects by tag without depending on
 * that module. VFX is purely local/cosmetic — driven by already-replicated gameplay, never replicated.
 */
class DESIGNPATTERNSSEAMS_API ISeam_VfxController
{
	GENERATED_BODY()

public:
	/** Spawn a one-shot VFX (by tag) at a world location/rotation. Returns an invalid handle for one-shots. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Vfx")
	FSeam_VfxHandle SpawnVfxAtLocation(FGameplayTag VfxTag, FVector Location, FRotator Rotation);

	/** Spawn a VFX attached to a component/socket. Returns a handle so it can be stopped. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Vfx")
	FSeam_VfxHandle SpawnVfxAttached(FGameplayTag VfxTag, USceneComponent* AttachTo, FName Socket);

	/** Stop a previously-spawned attached/looping VFX. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Vfx")
	void StopVfx(FSeam_VfxHandle Handle);
};
