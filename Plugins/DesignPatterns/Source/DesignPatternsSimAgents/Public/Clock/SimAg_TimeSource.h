// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimAg_TimeSource.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_TimeSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Time-SOURCE seam: the contract a genre clock (e.g. a Survival day/night clock) implements so the
 * sim-agents clock can DERIVE its calendar from that authoritative source instead of owning time.
 *
 * Direction of dependency matters:
 *   - ISeam_SimClock (in DesignPatternsSeams) is what CONSUMERS read — "tell me the sim time".
 *     USimAg_ClockSubsystem implements ISeam_SimClock and publishes it as a service.
 *   - ISimAg_TimeSource (this) is what a genre clock PROVIDES — "I already own the day/night phase,
 *     bind me and stop running your own ticker". USimAg_ClockSubsystem CONSUMES one of these.
 *
 * Keeping the two interfaces distinct avoids a cycle: a Survival clock implements ISimAg_TimeSource
 * (a thin adapter, no SimAgents include) and the SimAgents clock re-publishes the unified time as
 * ISeam_SimClock to everyone else. When no time source is bound, the SimAgents clock owns time itself.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_TimeSource
{
	GENERATED_BODY()

public:
	/**
	 * Time of day in [0,1) (0 = midnight, 0.5 = midday). The SimAgents clock multiplies this by its
	 * configured HoursPerDay to produce the calendar Hour/Minute, so a source with any internal day
	 * length still drives a consistent schedule.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Clock")
	float GetNormalizedTimeOfDay() const;

	/** Whole in-sim days elapsed since the source started (the calendar day index). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Clock")
	int32 GetDayNumber() const;
};
