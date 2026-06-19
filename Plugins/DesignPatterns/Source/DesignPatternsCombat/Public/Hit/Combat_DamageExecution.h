// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_DamageExecution.generated.h"

/**
 * Strategy-style, pluggable damage calculator (GAS-FREE).
 *
 * EditInlineNew + Instanced: a hitbox/attack holds a concrete subclass instance that the
 * designer authors inline. CalculateDamage is a BlueprintNativeEvent so projects override
 * the formula in C++ or Blueprint without touching the hitbox code — the classic Strategy
 * pattern, mirroring the core UDP_Strategy approach but specialised for combat damage.
 *
 * The default implementation returns the hit's BaseDamage, applying a simple crit roll and
 * a True-damage passthrough. Subclasses can add armor/resistance, falloff, multipliers, etc.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, BlueprintType, DefaultToInstanced)
class DESIGNPATTERNSCOMBAT_API UCombat_DamageExecution : public UObject
{
	GENERATED_BODY()

public:
	/** Probability [0,1] that a hit is a critical strike. 0 disables crits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Damage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CritChance = 0.f;

	/** Multiplier applied to BaseDamage on a critical strike. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Damage", meta = (ClampMin = "1.0"))
	float CritMultiplier = 2.f;

	/**
	 * Compute final damage for a confirmed hit. AUTHORITY context (called server-side by the
	 * hitbox), but the method itself is pure math so it is safe to call anywhere for prediction.
	 * @param Hit the server-confirmed hit (carries BaseDamage + DamageType + actors).
	 * @return final damage to apply via the victim's UCombat_HealthComponent.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatternsCombat|Damage")
	float CalculateDamage(const FCombat_HitResult& Hit) const;
	virtual float CalculateDamage_Implementation(const FCombat_HitResult& Hit) const;

protected:
	/** Deterministic-friendly crit roll helper (uses FMath::FRand by default). */
	virtual bool RollCritical() const;
};
