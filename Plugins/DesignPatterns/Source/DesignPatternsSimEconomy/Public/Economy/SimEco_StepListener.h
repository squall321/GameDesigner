// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimEco_StepListener.generated.h"

UINTERFACE(MinimalAPI)
class USimEco_StepListener : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam implemented by anything the economy driver must advance by a discrete fixed step (facility
 * production queues, consumer drains). The driver iterates registered listeners once per fixed step
 * and calls AdvanceEconomyStep BEFORE clearing the market, so production/consumption that lands in a
 * stockpile this step is reflected in the orders the market clears.
 *
 * This is a NATIVE-only interface (no BlueprintNativeEvent) called only on the authoritative server
 * from the driver's fixed-step loop — implementers must still guard their own authority.
 */
class DESIGNPATTERNSSIMECONOMY_API ISimEco_StepListener
{
	GENERATED_BODY()

public:
	/**
	 * Advance this participant by one fixed economy step. Called on the server only, in registration
	 * order, before the market clears.
	 *
	 * @param StepSeconds  The fixed-step duration in simulation seconds.
	 * @param StepIndex    The monotonic index of the step being run.
	 * @param DayNumber    The sim-clock day number for this step (for day-gated logic).
	 */
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) = 0;
};
