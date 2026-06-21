// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/Combat_StatusEffect.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Combat_StatusFamilyEffect.generated.h"

/**
 * How a new application of an effect that shares a FAMILY with an active one is resolved by the
 * UCombat_StatusStackController. The base UCombat_StatusEffectComponent::ApplyEffect already refreshes
 * same-TAG effects; this policy governs cross-effect, same-FAMILY behavior on top of that.
 */
UENUM(BlueprintType)
enum class ECombat_StackPolicy : uint8
{
	/** Re-applying refreshes duration (the component's built-in same-tag behavior). No stack count. */
	Refresh,
	/** Each application increments a family stack count up to MaxStacks (intensifying the effect). */
	Stack,
	/** Each application is an independent instance (no merging); the controller just tracks the count. */
	Independent
};

/**
 * A status effect that belongs to a FAMILY (e.g. all bleeds share DP.Combat.Status.Family.Bleed) and
 * participates in cross-effect stacking, diminishing-returns and immunity, coordinated by
 * UCombat_StatusStackController.
 *
 * It extends the shipped UCombat_StatusEffect ADDITIVELY — all existing fields/hooks remain. The
 * family/stack metadata here is READ by the controller; the effect's own OnApply/OnTick/OnRemove are
 * unchanged and still run via the existing component.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNSCOMBAT_API UCombat_StatusFamilyEffect : public UCombat_StatusEffect
{
	GENERATED_BODY()

public:
	/** Family this effect belongs to. Effects sharing a family stack / diminish / immunize together. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Family")
	FGameplayTag FamilyTag;

	/** How re-application within the family is resolved. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Family")
	ECombat_StackPolicy StackPolicy = ECombat_StackPolicy::Refresh;

	/** Maximum stacks for the Stack policy (>=1). Ignored for Refresh/Independent. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Family", meta = (ClampMin = "1"))
	int32 MaxStacks = 5;

	/**
	 * Diminishing-returns curve: input is the current family stack/application count, output is a
	 * multiplier in [0,1] applied to the effective Duration of subsequent applications (so the Nth
	 * application of crowd-control lasts less). Null curve = no DR (multiplier 1). Content-authored.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Family")
	TObjectPtr<UCurveFloat> DiminishingReturnsCurve;

	/**
	 * While this effect is active it grants immunity to this tag's family (an effect whose FamilyTag
	 * matches GrantsImmunityTag cannot be applied). Empty = grants no immunity.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Family")
	FGameplayTag GrantsImmunityTag;

	/**
	 * @return the duration multiplier for the application at the given existing family count, sampling
	 * DiminishingReturnsCurve. 1 when no curve is set (defensive fallback).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status|Family")
	float GetDurationMultiplierForStack(int32 ExistingCount) const;
};
