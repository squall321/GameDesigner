// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "HUD_DamageNumberStyleDataAsset.generated.h"

/**
 * One style row mapping a classification tag (crit / heal / weakpoint / block...) to the presentation
 * applied to a floating damage number of that kind: text color, scale multiplier, and an optional prefix
 * (e.g. "+" for heals, "!" for crits). Matched exactly or as a parent of the number's classification tag.
 *
 * No magic numbers in code — every visual is authored here.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_DamageNumberStyleRow
{
	GENERATED_BODY()

	/** Classification tag this style applies to (e.g. DP.Combat.Reaction.Crit). Empty rows are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|DamageNumber")
	FGameplayTag ClassificationTag;

	/** Text color for numbers of this kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|DamageNumber")
	FLinearColor Color = FLinearColor::White;

	/** Scale multiplier applied on top of the base font scale (1 = no change). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|DamageNumber", meta = (ClampMin = "0.1"))
	float ScaleMultiplier = 1.f;

	/** Optional text prefix shown before the amount (e.g. "+", "!"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|DamageNumber")
	FString Prefix;
};

/**
 * Data-driven style + tuning for floating combat text (UHUD_DamageNumberSubsystem).
 *
 * Holds every tunable the damage-number subsystem reads — pool cap, lifetime, rise/drift motion, the
 * reflected payload field names to read off the combat bus payload, the tag sets that classify a hit as
 * crit / heal / weakpoint, and the per-classification style rows — so NOTHING is hard-coded in the
 * subsystem. Identity is the inherited DataTag (resolved via the core data registry).
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Damage Number Style"))
class DESIGNPATTERNSHUD_API UHUD_DamageNumberStyleDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// --- Pool / lifetime ---

	/** Hard cap on simultaneously-active floating numbers; oldest is recycled when exceeded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Pool", meta = (ClampMin = "1"))
	int32 MaxConcurrent = 32;

	/** Seconds a floating number lives before it is recycled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Pool", meta = (ClampMin = "0.1"))
	float Lifetime = 1.2f;

	// --- Motion (screen-space, applied by the bound widget; the VM exposes the values) ---

	/** Screen-space pixels the number rises over its lifetime (negative Y = up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Motion")
	float RisePixels = 64.f;

	/** Max random horizontal jitter (pixels) applied at spawn so stacked hits fan out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Motion", meta = (ClampMin = "0.0"))
	float HorizontalJitterPixels = 18.f;

	/** Base font scale before the per-classification ScaleMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Motion", meta = (ClampMin = "0.1"))
	float BaseScale = 1.f;

	// --- Reflected payload field names (combat bus payload; no Combat include) ---
	// Defaults match the shipped FCombat_HitResult field names. A project with a different payload retargets
	// these without touching code.

	/** Field name of the VICTIM actor on the bus payload (default "HitActor"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Payload")
	FName VictimFieldName = FName(TEXT("HitActor"));

	/** Field name of the float damage amount on the bus payload (default "BaseDamage"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Payload")
	FName AmountFieldName = FName(TEXT("BaseDamage"));

	/** Field name of the FVector impact point on the bus payload (default "ImpactPoint"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Payload")
	FName ImpactPointFieldName = FName(TEXT("ImpactPoint"));

	/** Field name of the FGameplayTag classifying the hit on the bus payload (default "SourceTag"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Payload")
	FName ClassificationFieldName = FName(TEXT("SourceTag"));

	// --- Classification tag sets ---

	/** Source/classification tags that mark a hit as a critical (any match -> crit styling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Classify")
	FGameplayTagContainer CritTags;

	/** Source/classification tags that mark an event as a heal (positive, "+" styling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Classify")
	FGameplayTagContainer HealTags;

	/** Source/classification tags that mark a hit as a weak-point hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Classify")
	FGameplayTagContainer WeakPointTags;

	/** Per-classification style rows. The first matching row (exact, else parent) wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Style")
	TArray<FHUD_DamageNumberStyleRow> StyleRows;

	/** Default style used when no row matches a number's classification tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber|Style")
	FHUD_DamageNumberStyleRow DefaultStyle;

	/**
	 * Resolve the style for a classification tag: the most-specific matching StyleRow (exact, else the
	 * longest matching parent), or DefaultStyle when none matches.
	 */
	const FHUD_DamageNumberStyleRow& ResolveStyle(const FGameplayTag& Classification) const;

	/** Classify a hit's source/classification tag against the crit/heal/weakpoint sets. */
	bool IsCrit(const FGameplayTag& Classification) const { return CritTags.HasTag(Classification); }
	bool IsHeal(const FGameplayTag& Classification) const { return HealTags.HasTag(Classification); }
	bool IsWeakPoint(const FGameplayTag& Classification) const { return WeakPointTags.HasTag(Classification); }

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("HUD_DamageNumberStyle")); }
	//~ End UDP_DataAsset
};
