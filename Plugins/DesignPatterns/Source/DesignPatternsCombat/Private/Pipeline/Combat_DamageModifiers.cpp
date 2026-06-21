// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_DamageModifiers.h"
#include "Pipeline/Combat_ResistanceProfile.h"

// ---- Armor / Resistance ----

void UCombat_Mod_ArmorResistance::Modify_Implementation(FCombat_DamageContext& Context) const
{
	if (!OverrideProfile)
	{
		return;
	}

	// Add this profile's channel resistance to the running term. Resistances stack additively across
	// modifiers; the final fraction is clamped in FCombat_DamageContext::ComputeFinalDamage.
	Context.ResistFraction += OverrideProfile->GetResistFraction(Context.DamageChannel);
}

// ---- Flat + Percent ----

void UCombat_Mod_FlatPercent::Modify_Implementation(FCombat_DamageContext& Context) const
{
	Context.FlatBonus += FlatBonus;
	Context.MultiplierFactor *= (1.f + PercentBonus);
}

// ---- Critical ----

bool UCombat_Mod_Critical::RollCritical() const
{
	if (CritChance <= 0.f)
	{
		return false;
	}
	return FMath::FRand() < CritChance;
}

void UCombat_Mod_Critical::Modify_Implementation(FCombat_DamageContext& Context) const
{
	const bool bForced = bWeakpointForcesCrit && Context.bIsWeakpoint;
	const bool bCrit = bForced || Context.bIsCritical || RollCritical();
	if (bCrit)
	{
		Context.bIsCritical = true;
		Context.MultiplierFactor *= FMath::Max(1.f, CritMultiplier);
	}
}

// ---- DoT Conversion ----

void UCombat_Mod_DotConversion::Modify_Implementation(FCombat_DamageContext& Context) const
{
	// PURE: flag the fraction only. The pipeline component reads DotFraction post-pass and applies the
	// actual status effect (it owns the world side effects). Accumulate so multiple converters compose,
	// clamped to a full conversion.
	Context.DotFraction = FMath::Clamp(Context.DotFraction + ConversionFraction, 0.f, 1.f);
}

// ---- Poise Damage ----

void UCombat_Mod_PoiseDamage::Modify_Implementation(FCombat_DamageContext& Context) const
{
	float Poise = PoiseDamage;
	if (Context.bIsCritical)
	{
		Poise *= FMath::Max(1.f, CritPoiseMultiplier);
	}

	// Blocked hits transfer no poise damage to the victim (guard absorbs the stagger pressure).
	if (Context.bWasBlocked || Context.bWasParried || Context.bWasInvulnerable)
	{
		return;
	}

	Context.PoiseDamage += Poise;
}
