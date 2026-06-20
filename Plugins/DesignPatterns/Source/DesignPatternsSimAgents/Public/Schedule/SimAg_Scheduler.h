// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Schedule/SimAg_ScheduleAsset.h"
#include "SimAg_Scheduler.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_Scheduler : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only scheduler seam: "what is this agent supposed to be doing right now, and what would it be
 * doing at an arbitrary hour?". The agent brain (utility-AI selector) reads the current activity
 * through this interface as ONE more input to its scoring, without depending on the concrete
 * USimAg_ScheduleComponent — so a schedule can be supplied by any implementer (a component, a squad
 * coordinator, a quest script) and swapped freely.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_Scheduler
{
	GENERATED_BODY()

public:
	/**
	 * The activity the agent should currently be performing, per its schedule and the sim clock.
	 * Returns an empty tag if no schedule is assigned or it has no entries.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Schedule")
	FGameplayTag GetCurrentActivity() const;

	/**
	 * The full schedule entry (activity + location) that would be active at HourOfDay, in
	 * [0, HoursPerDay). Lets planners look ahead (e.g. "head home before the Sleep slot").
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Schedule")
	FSimAg_ScheduleEntry GetScheduleEntryForHour(float HourOfDay) const;
};
