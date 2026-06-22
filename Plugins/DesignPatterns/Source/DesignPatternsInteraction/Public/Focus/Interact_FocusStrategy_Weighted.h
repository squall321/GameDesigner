// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Focus/Interact_FocusStrategy.h"
#include "Interact_FocusStrategy_Weighted.generated.h"

/**
 * Best-target arbitration that scores each candidate by a designer-weighted blend of inverse
 * distance, inverse angle, a line-of-sight bonus, and authored Priority. Drops in beside the shipped
 * Closest / LineOfSight / Cone / Priority strategies as an additional inline-instanced choice.
 *
 * Pure function of the candidate list + query (it never re-traces), reading only fields already on
 * FInteract_Candidate. All weights are EditAnywhere tunables — no magic constants in code. Higher
 * total score wins; ties broken by closest distance.
 */
UCLASS(meta = (DisplayName = "Focus: Weighted Best"))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusStrategy_Weighted : public UInteract_FocusStrategy
{
	GENERATED_BODY()

public:
	/** Weight applied to the normalized inverse-distance term (closer = higher contribution). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "0.0"))
	float DistanceWeight = 1.0f;

	/** Weight applied to the normalized inverse-angle term (more centered = higher contribution). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "0.0"))
	float AngleWeight = 1.0f;

	/** Flat bonus added to a candidate's score when it has confirmed line of sight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "0.0"))
	float LineOfSightBonus = 0.5f;

	/** Weight applied to the (normalized) authored Priority term. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "0.0"))
	float PriorityWeight = 1.0f;

	/**
	 * Distance (cm) used to normalize the inverse-distance term so DistanceWeight is comparable across
	 * scales. A candidate at this distance contributes ~half of the full distance term. Tunable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "1.0", Units = "cm"))
	float DistanceNormalization = 250.f;

	/** Priority value treated as "full" Priority contribution when normalizing the priority term. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Focus|Weights", meta = (ClampMin = "1"))
	int32 PriorityNormalization = 10;

	virtual int32 SelectBestCandidate_Implementation(const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const override;
};
