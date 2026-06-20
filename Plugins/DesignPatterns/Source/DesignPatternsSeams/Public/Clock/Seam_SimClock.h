// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_SimClock.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_SimClock : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared simulation-clock seam. ONE definition shared by every system that needs simulation time
 * (economy step accumulator, grid timed growth, agent schedules) and adapted onto the Survival
 * day-night clock. Whoever owns the authoritative clock implements this; consumers read it through
 * a TScriptInterface<ISeam_SimClock> so they never depend on a concrete clock type.
 *
 * Consumers drive their own fixed-step accumulators from RealDelta * GetTimeScale() and skip while
 * IsPaused() — so time scale / pause are honoured uniformly without each system re-implementing them.
 */
class DESIGNPATTERNSSEAMS_API ISeam_SimClock
{
	GENERATED_BODY()

public:
	/** Current simulation time multiplier (1.0 = real time, 0 = effectively paused). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Clock")
	double GetTimeScale() const;

	/** True when simulation time is paused (consumers should not advance their accumulators). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Clock")
	bool IsPaused() const;

	/** Time of day in [0,1) (0 = midnight). Lets lighting/schedule systems share one day phase. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Clock")
	float GetNormalizedTimeOfDay() const;

	/** Whole days elapsed since simulation start (the calendar day index). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Clock")
	int32 GetDayNumber() const;
};
