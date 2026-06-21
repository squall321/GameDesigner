// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Prog_Condition.generated.h"

/**
 * Abstract, inline-editable "is this satisfied?" leaf — the Strategy pattern for achievement unlock
 * rules. A UProg_AchievementDefinition holds a list of these; an achievement unlocks when EVERY
 * condition's Evaluate returns true.
 *
 * Conditions are stateless and READ-ONLY: they query state held elsewhere (the achievement subsystem's
 * bus-fed counter/flag accumulator, resolved from the WorldContext), never mutate anything. This keeps
 * them safe to evaluate any number of times, on any peer.
 *
 * Ships three concrete strategies (below). Projects add their own by subclassing in C++ or Blueprint
 * (overriding the BlueprintNativeEvent Evaluate).
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNSPROGRESSION_API UProg_Condition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate this condition against current state.
	 * @param WorldContext any object with a world (the achievement subsystem passes itself), used to
	 *                     resolve the counter/flag accumulator. May be null in editor/CDO contexts, in
	 *                     which case a well-behaved implementation returns false.
	 * @return true if the condition is currently satisfied.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Progression|Achievement")
	bool Evaluate(UObject* WorldContext) const;
	virtual bool Evaluate_Implementation(UObject* WorldContext) const;

	/** A short human-readable description for tooling/debug overlays. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Progression|Achievement")
	FText GetConditionDescription() const;
	virtual FText GetConditionDescription_Implementation() const;

	/**
	 * Optional normalized progress [0,1] toward satisfying this condition, for progress UIs and the
	 * optional platform-progress report. Default returns Evaluate ? 1 : 0; counter conditions override
	 * with a real fraction.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Progression|Achievement")
	float GetProgressFraction(UObject* WorldContext) const;
	virtual float GetProgressFraction_Implementation(UObject* WorldContext) const;

protected:
	/**
	 * Resolve the achievement subsystem's flag value for FlagTag (false if unresolved). Shared helper
	 * used by the flag/counter strategies so they don't each duplicate the resolve.
	 */
	bool ResolveHubFlag(UObject* WorldContext, const FGameplayTag& FlagTag) const;

	/** Resolve the achievement subsystem's counter value for CounterTag (0 if unresolved). */
	int64 ResolveHubCounter(UObject* WorldContext, const FGameplayTag& CounterTag) const;
};

/**
 * Satisfied when a named boolean flag has been raised. The flag is set when any matching message
 * arrives on the watched bus channel (the achievement subsystem records it). Use for one-shot,
 * "did event X ever happen this session/save" achievements (e.g. "reached the summit").
 */
UCLASS(meta = (DisplayName = "Hub Flag Is Set"))
class DESIGNPATTERNSPROGRESSION_API UProg_Condition_HubFlag : public UProg_Condition
{
	GENERATED_BODY()

public:
	/** The flag identity to test. Set true when a message arrives on a channel matching this tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement")
	FGameplayTag FlagTag;

	/** When true, the condition is satisfied while the flag is UNset (negation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement")
	bool bInvert = false;

	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;
};

/**
 * Satisfied when an accumulated counter reaches a threshold. The counter increments by a message's
 * reported delta (or 1) each time a matching message arrives. Use for "do X N times" achievements
 * (e.g. "defeat 100 enemies").
 */
UCLASS(meta = (DisplayName = "Hub Counter >= Threshold"))
class DESIGNPATTERNSPROGRESSION_API UProg_Condition_HubCounterAtLeast : public UProg_Condition
{
	GENERATED_BODY()

public:
	/** The counter identity to test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement")
	FGameplayTag CounterTag;

	/** Satisfied once the counter is >= this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement", meta = (ClampMin = "1"))
	int64 Threshold = 1;

	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual float GetProgressFraction_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;
};

/**
 * Satisfied when the count of messages seen on a specific bus channel reaches a threshold.
 *
 * Differs from HubCounterAtLeast in WHAT it counts: this counts raw message OCCURRENCES on the
 * configured ChannelTag itself (the subsystem keeps a per-channel hit count), independent of any
 * delta the payload carries. Use for "the event X fired N times" where the payload has no count
 * (e.g. "open 10 chests").
 */
UCLASS(meta = (DisplayName = "Bus Channel Fired >= Threshold"))
class DESIGNPATTERNSPROGRESSION_API UProg_Condition_BusCounter : public UProg_Condition
{
	GENERATED_BODY()

public:
	/** The bus channel whose occurrences are counted (child-matching). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement")
	FGameplayTag ChannelTag;

	/** Satisfied once the channel has fired at least this many times. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Achievement", meta = (ClampMin = "1"))
	int64 Threshold = 1;

	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual float GetProgressFraction_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;
};
