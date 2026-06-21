// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

#include "Tut_Condition.generated.h"

/**
 * Evaluation context handed to a UTut_Condition.
 *
 * A tutorial/hint condition cannot reach into the world by itself — it is a pure, instanced policy object.
 * Instead the runner (UTut_TutorialSubsystem / UTut_HintSubsystem) implements this interface and is passed as
 * the WorldContext to Evaluate, so the condition can ask its owner two questions without coupling to either
 * subsystem's concrete type:
 *  - "has a one-shot bus event with this tag been seen since this condition was armed?" (HasSeenBusEvent)
 *  - "what is the world-hub queryable read seam I should consult?" (provided indirectly via the context's
 *    world-context object, which the hub conditions resolve from the service locator).
 *
 * The interface is a native C++ interface (no Blueprint implementation): the runner subsystems implement it.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTut_ConditionContext : public UInterface
{
	GENERATED_BODY()
};

/** @see UTut_ConditionContext */
class DESIGNPATTERNSTUTORIAL_API ITut_ConditionContext
{
	GENERATED_BODY()

public:
	/**
	 * @return true if a one-shot bus event whose channel matches EventTag (exact-or-child) has been observed
	 * since the currently-evaluated condition set was armed. The runner records seen event tags as bus
	 * messages arrive and clears the record when it advances to a new step / re-arms a hint.
	 */
	virtual bool HasSeenBusEvent(const FGameplayTag& EventTag) const = 0;

	/**
	 * Read an effective world-hub value for Key (global scope) into Out via the IWorldHub_Queryable seam.
	 * @return true if a value (stored or default) was produced; false if the seam is unresolved or has no value.
	 * The Out value is an FInstancedStruct decodable through FSeam_NetValue::FromInstancedStruct for the
	 * replicable kinds the hub flag conditions consult.
	 */
	virtual bool QueryHubValue(const FGameplayTag& Key, FInstancedStruct& Out) const = 0;
};

/**
 * Abstract, instanced policy object that answers a single yes/no question about current game state.
 *
 * Conditions drive the tutorial runner (a step's Trigger surfaces it; its Completion advances past it) and
 * the hint system (a hint's condition gates whether it may surface). They are authored INLINE inside a data
 * asset (EditInlineNew) so designers compose tutorials/hints without code, and they hold no gameplay
 * pointers — all state is read through the WorldContext (an ITut_ConditionContext runner) at evaluation time.
 *
 * Subclass and override Evaluate to add new condition kinds. The shipped concrete kinds are:
 *  - UTut_Condition_BusEvent      : a one-shot bus event tag has been seen since arm.
 *  - UTut_Condition_HubFlag       : a world-hub boolean flag equals an expected value.
 *  - UTut_Condition_HubCounterAtLeast : a world-hub integer/counter flag is at least a threshold.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories)
class DESIGNPATTERNSTUTORIAL_API UTut_Condition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate this condition against current state.
	 *
	 * @param WorldContext the runner driving evaluation; expected to implement ITut_ConditionContext (and to
	 *        be a valid world-context object for subsystem resolution). A null or non-context object yields
	 *        false (a condition that cannot be evaluated is treated as not-yet-met).
	 * @return true if the condition is currently satisfied.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tutorial|Condition")
	bool Evaluate(UObject* WorldContext) const;
	virtual bool Evaluate_Implementation(UObject* WorldContext) const;

	/** A short human-readable description of this condition for debug strings / tooling. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Tutorial|Condition")
	FString DescribeCondition() const;
	virtual FString DescribeCondition_Implementation() const;

protected:
	/** Resolve the condition context from a world-context object, or null if it does not implement it. */
	static const ITut_ConditionContext* GetContext(const UObject* WorldContext);
};

/**
 * Satisfied once a one-shot message-bus event whose channel matches EventTag has been seen since the owning
 * condition set was armed. This is how "the player performed action X" triggers/completions are authored:
 * gameplay broadcasts a DP.Bus.* event, the runner records it, and this condition reports it as seen.
 */
UCLASS(meta = (DisplayName = "Bus Event Seen"))
class DESIGNPATTERNSTUTORIAL_API UTut_Condition_BusEvent : public UTut_Condition
{
	GENERATED_BODY()

public:
	//~ Begin UTut_Condition
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FString DescribeCondition_Implementation() const override;
	//~ End UTut_Condition

	/** The bus channel tag that, once observed (exact-or-child), satisfies this condition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Condition", meta = (Categories = "DP.Bus"))
	FGameplayTag EventTag;
};

/**
 * Satisfied when a world-hub boolean flag (read via IWorldHub_Queryable, global scope) equals bExpected.
 * Used to gate steps/hints on persistent, already-replicated world state (e.g. "first quest accepted").
 */
UCLASS(meta = (DisplayName = "Hub Boolean Flag"))
class DESIGNPATTERNSTUTORIAL_API UTut_Condition_HubFlag : public UTut_Condition
{
	GENERATED_BODY()

public:
	//~ Begin UTut_Condition
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FString DescribeCondition_Implementation() const override;
	//~ End UTut_Condition

	/** The world-hub flag key to read (must identify a Bool flag for a meaningful result). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Condition", meta = (Categories = "DP.WorldHub.Flag"))
	FGameplayTag FlagKey;

	/** The boolean value the flag must equal for this condition to be satisfied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Condition")
	bool bExpected = true;
};

/**
 * Satisfied when a world-hub integer/counter flag (read via IWorldHub_Queryable, global scope) is at least
 * Threshold. Used for "collect N of something" style progression gates.
 */
UCLASS(meta = (DisplayName = "Hub Counter At Least"))
class DESIGNPATTERNSTUTORIAL_API UTut_Condition_HubCounterAtLeast : public UTut_Condition
{
	GENERATED_BODY()

public:
	//~ Begin UTut_Condition
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FString DescribeCondition_Implementation() const override;
	//~ End UTut_Condition

	/** The world-hub counter/integer flag key to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Condition", meta = (Categories = "DP.WorldHub.Flag"))
	FGameplayTag CounterKey;

	/** The inclusive lower bound the counter must reach for this condition to be satisfied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial|Condition")
	int64 Threshold = 1;
};
