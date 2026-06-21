// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_MovementController.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_MovementController : public UInterface
{
	GENERATED_BODY()
};

/**
 * Movement intent seam, so the movement state machine is driven identically by player input and by AI —
 * the movement component reads intent through this seam and never assumes a controller type. A player
 * input component and an AI movement driver both implement it.
 */
class DESIGNPATTERNSSEAMS_API ISeam_MovementController
{
	GENERATED_BODY()

public:
	/** Desired movement direction/magnitude in world space (zero = idle). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Move")
	FVector GetMoveIntent() const;

	/** Desired facing (for strafe/aim-relative movement). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Move")
	FRotator GetFacingIntent() const;

	/** True while the sprint modifier is held/desired. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Move")
	bool IsSprintHeld() const;

	/** True while crouch is held/desired. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Move")
	bool IsCrouchHeld() const;

	/**
	 * Consume a pending special-move request (dash/dodge/mantle/vault), returning true and writing the
	 * request tag if one was queued. The movement component polls this each tick; consuming clears it so
	 * a request fires exactly once.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Move")
	bool ConsumeSpecialMoveRequest(FGameplayTag& OutRequestTag);
};
