// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_EncumbranceCurve.generated.h"

class UCurveFloat;

/**
 * One encumbrance tier: when carried weight as a fraction of capacity reaches LoadFractionThreshold, the
 * tier activates, tagging the state and applying MoveSpeedMultiplier to the move-speed attribute.
 *
 * Tiers are evaluated highest-threshold-first; the first satisfied tier wins. MoveSpeedMultiplier is a
 * multiplicative delta consumed as (1 + delta) by the stats fold, so authoring -0.3 means "70% speed".
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EncumbranceTier
{
	GENERATED_BODY()

	/** State tag for this tier (e.g. "RPG.Encumbrance.Overloaded"); reported by GetEncumbranceTier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Encumbrance")
	FGameplayTag TierTag;

	/** Carried/capacity fraction at which this tier activates (e.g. 0.8 = at 80% load). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Encumbrance", meta = (ClampMin = "0.0"))
	float LoadFractionThreshold = 0.f;

	/** Multiplicative move-speed delta applied while this tier is active (e.g. -0.3 => 70% speed). 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Encumbrance")
	float MoveSpeedMultiplier = 0.f;

	FRPG_EncumbranceTier() = default;
};

/**
 * Data-driven encumbrance configuration: base carry capacity, an optional capacity-per-strength curve and the
 * tiered penalties. Identity = inherited DataTag. No magic numbers — all thresholds/penalties are authored.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_EncumbranceCurve : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Base carry capacity (weight units) before any strength bonus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Encumbrance", meta = (ClampMin = "0.0"))
	float BaseCapacity = 0.f;

	/**
	 * Optional curve mapping the Strength attribute to additional capacity (X = strength, Y = bonus
	 * capacity). When absent, capacity is BaseCapacity only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Encumbrance")
	TObjectPtr<UCurveFloat> CapacityPerStrength;

	/** Encumbrance tiers (authored in any order; evaluated highest-threshold-first). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Encumbrance")
	TArray<FRPG_EncumbranceTier> Tiers;

	/** Total capacity given a Strength value (BaseCapacity + curve bonus). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Encumbrance")
	float GetCapacityForStrength(float Strength) const;

	/**
	 * Resolve the active tier for a carried/capacity load fraction. Returns the highest-threshold tier whose
	 * threshold is met. OutMoveSpeedMultiplier carries that tier's penalty (0 when no tier is active).
	 */
	FGameplayTag ResolveTier(float LoadFraction, float& OutMoveSpeedMultiplier) const;

	//~ Begin UDP_DataAsset
	/** Collapse all encumbrance curves into one shared "RPG_EncumbranceCurve" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
