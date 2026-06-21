// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Combat_ResistanceProfile.generated.h"

/**
 * One resistance entry: how much a given damage channel is reduced, expressed as a fraction in
 * [0,1] (0 = no reduction, 1 = immune). Authored on a UCombat_ResistanceProfile data asset so all
 * resistance numbers live in content, never hardcoded.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_ResistanceEntry
{
	GENERATED_BODY()

	/** The damage channel this entry resists (e.g. DP.Combat.Damage.Fire). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance")
	FGameplayTag DamageChannel;

	/** Fraction of damage of this channel removed, in [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ResistFraction = 0.f;

	FCombat_ResistanceEntry() = default;
};

/**
 * Designer-authored, per-archetype resistance/weakness table consumed by UCombat_Mod_ArmorResistance.
 *
 * It is a UDP_DataAsset (tag-identified content) so an enemy/material archetype maps to a shared,
 * registry-addressable profile rather than per-instance magic numbers. Lookups are by damage channel
 * tag; a missing channel resolves to zero resistance (a documented defensive fallback, not a magic
 * number). Negative weakness can be modeled by giving a channel a NEGATIVE resist via the optional
 * Weaknesses map (applied as an additive multiplier bump).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSCOMBAT_API UCombat_ResistanceProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Per-channel damage reduction fractions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance")
	TArray<FCombat_ResistanceEntry> Resistances;

	/**
	 * Optional flat armor value subtracted (as a resistance fraction) for the Physical channel, scaled
	 * by ArmorEffectiveness. Lets designers express a single "armor rating" without a per-channel row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance", meta = (ClampMin = "0.0"))
	float Armor = 0.f;

	/** How much one point of Armor contributes to the physical resist fraction (content-tunable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance", meta = (ClampMin = "0.0"))
	float ArmorEffectiveness = 0.005f;

	/** Hard cap on the resistance fraction any single channel can reach (defensive clamp). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Resistance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxResistFraction = 0.9f;

	/**
	 * @return the resist fraction for Channel in [0, MaxResistFraction]. Combines the per-channel
	 * entry with the Armor term (Physical only). Missing channel -> 0 (defensive fallback).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Resistance")
	float GetResistFraction(FGameplayTag Channel) const;
};
