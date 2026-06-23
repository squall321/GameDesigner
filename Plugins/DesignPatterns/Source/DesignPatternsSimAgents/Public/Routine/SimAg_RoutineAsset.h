// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimAg_RoutineAsset.generated.h"

/**
 * One step of a daily routine: from StartHour onward the agent performs Activity at Location. Richer than
 * a schedule entry: it carries an interruptible flag and the engine uses the agent's travel speed estimate
 * to leave EARLY so the agent ARRIVES at Location by StartHour (travel lead-time), rather than only
 * starting to walk at StartHour.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_RoutineStep
{
	GENERATED_BODY()

	/** Hour-of-day the agent should BE doing this, in [0, HoursPerDay). Fractional hours allowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Routine", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "24.0"))
	float StartHour = 0.f;

	/** What the agent does during this slot (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Routine", meta = (Categories = "SimAg.Activity"))
	FGameplayTag Activity;

	/** Where the agent should be for this slot (child of SimAg.Location). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Routine", meta = (Categories = "SimAg.Location"))
	FGameplayTag Location;

	/**
	 * Whether the agent may be interrupted (e.g. to flee) during this slot. A non-interruptible slot
	 * (e.g. a critical ritual) is held even under a flee request. Default true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Routine")
	bool bInterruptible = true;

	FSimAg_RoutineStep() = default;

	/** Sort predicate by StartHour, used to keep the timeline ordered for resolution. */
	bool operator<(const FSimAg_RoutineStep& Other) const { return StartHour < Other.StartHour; }
};

/**
 * A tag-identified daily routine asset: an ordered list of FSimAg_RoutineStep mapping any hour of the day
 * to an activity/location with interruptibility. Authored once and shared; resolved by tag through the
 * core data registry like USimAg_ScheduleAsset. Genre-neutral.
 *
 * Unlike a plain schedule, the routine COMPONENT uses each step's travel lead-time so an agent leaves
 * early enough to arrive on the hour. This asset itself is pure data; the lead-time math lives on the
 * component (it needs the agent's position).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMAGENTS_API USimAg_RoutineAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimAg_RoutineAsset();

	/** The day's routine steps. Need not be pre-sorted; resolution sorts a local copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Routine")
	TArray<FSimAg_RoutineStep> Steps;

	/**
	 * Resolve the step active at FractionalDays (using HoursPerDay to extract the hour-of-day). Picks the
	 * step with the greatest StartHour <= current hour; wraps to the last step before midnight. Returns a
	 * default (empty) step if Steps is empty.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Routine")
	FSimAg_RoutineStep ResolveStepForTime(double FractionalDays, int32 HoursPerDay) const;

	/**
	 * The NEXT step after the one active at the given time, and how many hours until it begins (>= 0).
	 * Lets the component start travelling early. Returns false if Steps is empty.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Routine")
	bool ResolveUpcomingStep(double FractionalDays, int32 HoursPerDay, FSimAg_RoutineStep& OutStep, float& OutHoursUntil) const;

	//~ Begin UDP_DataAsset
	/** Group all routine assets under one asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on out-of-range hours and steps with no Activity. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
