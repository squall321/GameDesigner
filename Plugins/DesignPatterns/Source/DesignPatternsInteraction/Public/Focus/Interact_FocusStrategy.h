// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/Interact_Types.h"
#include "Interact_FocusStrategy.generated.h"

/**
 * Strategy object (Strategy pattern) that picks the single "best" interactable from a detected
 * candidate set. EditInlineNew + Abstract so it is authored as an inline instanced subobject on the
 * interactor component (designers swap the concrete strategy in the component's details panel).
 *
 * Stateless: SelectBestCandidate is a pure function of the candidate list + query. Returns the index
 * of the chosen candidate, or INDEX_NONE when nothing should be focused.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories)
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Choose the best candidate index for the given query, or INDEX_NONE if none qualifies.
	 * Implementations must treat the inputs as read-only and must not assume any candidate is valid
	 * without checking IsValidCandidate().
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact|Focus")
	int32 SelectBestCandidate(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const;
	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const;
};

/**
 * Picks the geometrically closest candidate to the view location. Ties broken by smaller angle.
 * Ignores candidates without line of sight only if the candidate itself was flagged that way by
 * detection (the strategy does not re-trace).
 */
UCLASS(meta = (DisplayName = "Focus: Closest"))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy_Closest : public UInteract_FocusStrategy
{
	GENERATED_BODY()

public:
	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const override;
};

/**
 * Prefers candidates with confirmed line of sight, then among those picks the one most centered on
 * the view direction (smallest angle), then closest. Falls back to closest-overall if none has LOS.
 */
UCLASS(meta = (DisplayName = "Focus: Line Of Sight"))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy_LineOfSight : public UInteract_FocusStrategy
{
	GENERATED_BODY()

public:
	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const override;
};

/**
 * "Reticle" targeting: picks the candidate most tightly aligned with the view direction (smallest
 * angle), rejecting anything outside MaxConeHalfAngleDeg. Ties broken by distance.
 */
UCLASS(meta = (DisplayName = "Focus: Cone"))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy_Cone : public UInteract_FocusStrategy
{
	GENERATED_BODY()

public:
	/** Candidates whose AngleDeg exceeds this are ignored. Tunable, not a magic constant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float MaxConeHalfAngleDeg = 20.f;

	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const override;
};

/**
 * Picks the highest-priority candidate (Candidate.Priority, authored on the interactable). Ties
 * broken by closest distance. Use when some interactables should always win focus (e.g. a quest NPC
 * over scenery clutter) regardless of geometry.
 */
UCLASS(meta = (DisplayName = "Focus: Priority"))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy_Priority : public UInteract_FocusStrategy
{
	GENERATED_BODY()

public:
	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const override;
};
