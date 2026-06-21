// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat_WeakpointComponent.generated.h"

/**
 * One bone-keyed hitzone: maps a collision bone to a zone tag and a damage multiplier.
 * Authored on the component (or its data driving it); no hardcoded multipliers in code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_HitZone
{
	GENERATED_BODY()

	/** Bone name this zone covers (e.g. "head"). Matched case-insensitively against the hit bone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Weakpoint")
	FName BoneName = NAME_None;

	/** Classification of the zone (e.g. DP.Combat.Zone.Head). Surfaced for analytics / VFX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Weakpoint")
	FGameplayTag ZoneTag;

	/** Damage multiplier when a hit lands on this zone. >1 = weakpoint, <1 = armored zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Weakpoint", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.f;

	/** If true, a hit on this zone is treated as a weakpoint (sets Context.bIsWeakpoint). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Weakpoint")
	bool bIsWeakpoint = true;

	FCombat_HitZone() = default;
};

/**
 * Bone -> hitzone -> multiplier table.
 *
 * Purely a READ-ONLY query surface for the damage pipeline: it holds no replicated state and runs
 * the same on every machine (the bone->zone map is static authored content). The pure execution
 * calls QueryZone during its const pass; the side-effect owner never mutates anything here.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_WeakpointComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_WeakpointComponent();

	/** Authored zones. The first entry whose BoneName matches a hit bone wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Weakpoint")
	TArray<FCombat_HitZone> Zones;

	/**
	 * Resolve the hitzone for a bone. CONST / pure.
	 * @param BoneName        the bone the hit resolved to.
	 * @param OutMultiplier   the zone's damage multiplier (1 if no match).
	 * @param OutZoneTag      the zone's classification tag (empty if no match).
	 * @param OutIsWeakpoint  whether the matched zone is a weakpoint.
	 * @return true if a zone matched the bone.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Weakpoint")
	bool QueryZone(FName BoneName, float& OutMultiplier, FGameplayTag& OutZoneTag, bool& OutIsWeakpoint) const;
};
