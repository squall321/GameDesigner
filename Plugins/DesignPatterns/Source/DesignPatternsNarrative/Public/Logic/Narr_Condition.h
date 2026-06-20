// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Seam/Narr_StoryConditionSource.h"
#include "Narr_Condition.generated.h"

class ISeam_SimClock;

/**
 * The condition mini-language: composable, EditInlineNew narrative gates.
 *
 * This header is the SINGLE owner of the polymorphic condition types shared by the dialogue runner
 * (gates a dialogue node / choice) and the story director (gates a story beat). A condition is a pure
 * predicate authored inline inside a data asset, evaluated read-only against an INarr_StoryConditionSource
 * so it never depends on a concrete subsystem or on DesignPatternsWorld.
 *
 * Conditions form a tree: the composite leaf holds instanced children; the world-state leaves read the
 * source's flag/counter/beat accessors; the time-of-day leaf resolves the simulation clock itself and
 * fails closed when no clock is available. The tree is COSMETIC/LOCAL — it gates presentation and choice
 * availability and never mutates state (that is UNarr_Effect's job).
 *
 * IsMet is a BlueprintNativeEvent so projects can author bespoke conditions in Blueprint; the shipped
 * leaves implement the native _Implementation. Every leaf supports bInvert (logical NOT) and
 * bDefaultWhenNoSource (the conservative fail-closed result when no source is available).
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @return true if this condition is satisfied given Source.
	 *
	 * Source is a TScriptInterface so Blueprint condition subclasses can call back into it. A null/invalid
	 * source is handled defensively by each leaf: it returns bDefaultWhenNoSource (subject to bInvert).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Narrative|Condition")
	bool IsMet(const TScriptInterface<INarr_StoryConditionSource>& Source) const;
	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const;

	/** Value returned by IsMet when no usable source is available. Conservative default: false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	bool bDefaultWhenNoSource = false;

	/** When true the evaluated result is negated (logical NOT) before being returned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	bool bInvert = false;

	/** Human-readable summary for debug/log/editor tooltips. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Condition")
	virtual FString DescribeCondition() const;

protected:
	/** Apply bInvert to a raw evaluated result. Call from subclass IsMet overrides. */
	bool Finalize(bool bRawResult) const { return bInvert ? !bRawResult : bRawResult; }

	/** Resolve the raw native source pointer from a TScriptInterface, or null. */
	static INarr_StoryConditionSource* GetSource(const TScriptInterface<INarr_StoryConditionSource>& Source);
};

/** Comparison operators for the counter condition. */
UENUM(BlueprintType)
enum class ENarr_CounterCompare : uint8
{
	Less,
	LessEqual,
	Equal,
	GreaterEqual,
	Greater,
	NotEqual
};

/** How a composite condition's child conditions combine. */
UENUM(BlueprintType)
enum class ENarr_ConditionLogic : uint8
{
	/** All children must pass. */
	All,
	/** At least one child must pass. */
	Any
};

/** Leaf: passes when a world-hub boolean flag equals the expected value. */
UCLASS(meta = (DisplayName = "Flag Is Set"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_Flag : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The world-hub flag key to test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (Categories = "DP.WorldHub"))
	FGameplayTag FlagKey;

	/** The value the flag must hold for this condition to pass (before bInvert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	bool bExpected = true;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/** Leaf: passes when a world-hub counter compares against a threshold. */
UCLASS(meta = (DisplayName = "Counter Compare"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_Counter : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The world-hub counter key to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (Categories = "DP.WorldHub"))
	FGameplayTag CounterKey;

	/** The comparison applied as (CounterValue <op> Threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	ENarr_CounterCompare Compare = ENarr_CounterCompare::GreaterEqual;

	/** The right-hand side of the comparison. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	int64 Threshold = 1;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/** Leaf: passes when a named story beat/arc is active or completed (selectable). */
UCLASS(meta = (DisplayName = "Beat State"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_BeatState : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The beat or arc tag whose state is tested. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (Categories = "Narr.Beat,Narr.Arc"))
	FGameplayTag BeatOrArcTag;

	/** When true, requires the beat to be COMPLETED; when false, requires it to be currently ACTIVE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	bool bRequireCompleted = false;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/**
 * Leaf: true while the simulation clock's normalized time of day is within [StartTimeOfDay, EndTimeOfDay).
 *
 * Resolves an ISeam_SimClock from the service locator (under DP.Service.Clock) using the source's world
 * context. FAIL-CLOSED: when no clock is available it evaluates false (subject to bInvert) rather than
 * guessing a time. Supports a wrap-around window (Start > End, e.g. a night window 0.8..0.2).
 */
UCLASS(meta = (DisplayName = "Time Of Day"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_TimeOfDay : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** Window start in normalized time of day [0,1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"), Category = "DesignPatterns|Narrative|Condition")
	float StartTimeOfDay = 0.f;

	/** Window end in normalized time of day [0,1). When < Start the window wraps past midnight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0", ClampMax = "1.0"), Category = "DesignPatterns|Narrative|Condition")
	float EndTimeOfDay = 0.5f;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;

private:
	/** Resolve the sim-clock seam from the locator via Source's world context. OutClockObject set on hit. */
	static ISeam_SimClock* ResolveClock(const TScriptInterface<INarr_StoryConditionSource>& Source, UObject*& OutClockObject);
};

/** Composite: combines child conditions with All/Any logic. An empty composite passes (vacuous truth). */
UCLASS(meta = (DisplayName = "Composite (All / Any)"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_Composite : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** How the children combine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	ENarr_ConditionLogic Logic = ENarr_ConditionLogic::All;

	/** Inline child conditions. An empty list passes (before bInvert). */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	TArray<TObjectPtr<UNarr_Condition>> Children;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};
