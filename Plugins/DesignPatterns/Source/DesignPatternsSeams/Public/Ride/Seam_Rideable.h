// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_Rideable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_Rideable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-mostly seam describing a rideable/mountable (horse, vehicle, boat). The WorldSystems rideable
 * component implements it; AI mount-seekers, HUD interaction prompts and the camera read seat/occupancy
 * info through it. Entering/exiting a ride is an authority-validated mutation on the concrete component
 * (driven by a player-owned rider component), not a generic seam method.
 */
class DESIGNPATTERNSSEAMS_API ISeam_Rideable
{
	GENERATED_BODY()

public:
	/** The kind tag of this ride (e.g. Ride.Horse, Ride.Car) for filtering/prompts. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Ride")
	FGameplayTag GetRideKind() const;

	/** Total seats. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Ride")
	int32 GetSeatCount() const;

	/** Seats currently occupied. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Ride")
	int32 GetOccupiedSeatCount() const;

	/** True if at least one seat is free. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Ride")
	bool HasFreeSeat() const;
};
