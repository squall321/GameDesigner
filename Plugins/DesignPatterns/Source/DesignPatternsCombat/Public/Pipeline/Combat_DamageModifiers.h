// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Pipeline/Combat_DamageModifier.h"
#include "Combat_DamageModifiers.generated.h"

class UCombat_ResistanceProfile;

/**
 * Resistance/armor modifier. Resolves the victim's UCombat_ResistanceProfile and folds its
 * channel resist fraction into Context.ResistFraction. PURE: it only reads the profile (authored
 * content) and writes the running resist term — it never mitigates HP directly.
 *
 * The profile is resolved in priority order: an explicit override asset on this modifier wins;
 * otherwise the victim's UCombat_DefenseComponent / a profile component is consulted at runtime by
 * the execution and stashed on the context's victim — to stay PURE and component-include-free, this
 * leaf reads only the OverrideProfile here, while the pipeline component supplies the runtime profile
 * via a dedicated mod instance it configures. Designers typically set OverrideProfile per attack.
 */
UCLASS(meta = (DisplayName = "Armor / Resistance"))
class DESIGNPATTERNSCOMBAT_API UCombat_Mod_ArmorResistance : public UCombat_DamageModifier
{
	GENERATED_BODY()

public:
	/** Resistance table applied to the incoming channel. If null the modifier is a no-op. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TObjectPtr<UCombat_ResistanceProfile> OverrideProfile;

	virtual void Modify_Implementation(FCombat_DamageContext& Context) const override;
};

/**
 * Flat + percentage modifier. Adds FlatBonus to the context's flat term and multiplies the running
 * multiplier by (1 + PercentBonus). Both are content-authored; PURE.
 */
UCLASS(meta = (DisplayName = "Flat + Percent"))
class DESIGNPATTERNSCOMBAT_API UCombat_Mod_FlatPercent : public UCombat_DamageModifier
{
	GENERATED_BODY()

public:
	/** Flat amount added before multipliers. May be negative (a flat reduction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float FlatBonus = 0.f;

	/** Fractional multiplier bonus; 0.25 = +25%. May be negative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float PercentBonus = 0.f;

	virtual void Modify_Implementation(FCombat_DamageContext& Context) const override;
};

/**
 * Critical-strike modifier. Rolls (or honors a pre-set crit flag from a weakpoint) and, on a crit,
 * multiplies the running multiplier by CritMultiplier and sets Context.bIsCritical. PURE: the roll
 * uses a deterministic-friendly RNG hook so it can run identically for prediction if seeded.
 *
 * A weakpoint hit can FORCE a crit: if Context.bIsWeakpoint is already set and bWeakpointForcesCrit
 * is true, the crit is guaranteed regardless of CritChance.
 */
UCLASS(meta = (DisplayName = "Critical"))
class DESIGNPATTERNSCOMBAT_API UCombat_Mod_Critical : public UCombat_DamageModifier
{
	GENERATED_BODY()

public:
	/** Probability [0,1] of a crit. 0 disables the random roll (a forced weakpoint crit still applies). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CritChance = 0.f;

	/** Multiplier on a successful crit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "1.0"))
	float CritMultiplier = 2.f;

	/** If true, a weakpoint hit guarantees a crit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bWeakpointForcesCrit = true;

	virtual void Modify_Implementation(FCombat_DamageContext& Context) const override;

protected:
	/** Deterministic-friendly crit roll (FMath::FRand by default). Virtual for seeded prediction. */
	virtual bool RollCritical() const;
};

/**
 * DoT-conversion modifier. PURE: it merely FLAGS the fraction of final damage that should become a
 * damage-over-time effect (Context.DotFraction) and sets which status effect class to apply. The
 * actual UCombat_StatusEffectComponent::ApplyEffect call is performed by the authority-side
 * UCombat_DamagePipelineComponent — never here.
 */
UCLASS(meta = (DisplayName = "DoT Conversion"))
class DESIGNPATTERNSCOMBAT_API UCombat_Mod_DotConversion : public UCombat_DamageModifier
{
	GENERATED_BODY()

public:
	/** Fraction [0,1] of the final damage that converts to a DoT instead of instant HP loss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ConversionFraction = 0.5f;

	virtual void Modify_Implementation(FCombat_DamageContext& Context) const override;
};

/**
 * Poise-damage modifier. Adds a content-authored amount of poise damage to the context (independent
 * of HP damage). PURE — the actual poise application happens in the pipeline component.
 */
UCLASS(meta = (DisplayName = "Poise Damage"))
class DESIGNPATTERNSCOMBAT_API UCombat_Mod_PoiseDamage : public UCombat_DamageModifier
{
	GENERATED_BODY()

public:
	/** Poise damage this attack deals on a clean (un-blocked) hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0"))
	float PoiseDamage = 0.f;

	/** Multiplier applied to poise damage on a critical hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "1.0"))
	float CritPoiseMultiplier = 1.5f;

	virtual void Modify_Implementation(FCombat_DamageContext& Context) const override;
};
