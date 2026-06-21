// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_DamageContext.h"
#include "Combat_DeepNativeTags.h"

namespace
{
	/** Map the legacy ECombat_DamageType enum onto the deep-layer damage channel tag. */
	FGameplayTag DamageTypeToChannel(ECombat_DamageType Type)
	{
		switch (Type)
		{
		case ECombat_DamageType::Physical:  return CombatDeepNativeTags::Damage_Physical;
		case ECombat_DamageType::Fire:      return CombatDeepNativeTags::Damage_Fire;
		case ECombat_DamageType::Frost:     return CombatDeepNativeTags::Damage_Frost;
		case ECombat_DamageType::Lightning: return CombatDeepNativeTags::Damage_Lightning;
		case ECombat_DamageType::Poison:    return CombatDeepNativeTags::Damage_Poison;
		case ECombat_DamageType::True:      return CombatDeepNativeTags::Damage_True;
		case ECombat_DamageType::Generic:
		default:                            return CombatDeepNativeTags::Damage_Generic;
		}
	}
}

FCombat_DamageContext FCombat_DamageContext::FromHit(const FCombat_HitResult& InHit)
{
	FCombat_DamageContext Ctx;
	Ctx.Hit = InHit;
	Ctx.BaseDamage = InHit.BaseDamage;
	Ctx.DamageChannel = DamageTypeToChannel(InHit.DamageType);
	Ctx.FlatBonus = 0.f;
	Ctx.MultiplierFactor = 1.f;
	Ctx.ResistFraction = 0.f;
	Ctx.DotFraction = 0.f;
	Ctx.PoiseDamage = 0.f;
	return Ctx;
}

float FCombat_DamageContext::ComputeFinalDamage() const
{
	// Invulnerability / parry short-circuits to zero — the victim takes no HP damage.
	if (bWasInvulnerable || bWasParried)
	{
		return 0.f;
	}

	// True damage bypasses the resistance term entirely (still respects flat/multiplier shaping so
	// designers can still tune a True-damage attack's scaling).
	const float Resist = (DamageChannel == CombatDeepNativeTags::Damage_True)
		? 0.f
		: FMath::Clamp(ResistFraction, 0.f, 1.f);

	const float Shaped = (BaseDamage + FlatBonus) * FMath::Max(0.f, MultiplierFactor);
	const float Mitigated = Shaped * (1.f - Resist);
	return FMath::Max(0.f, Mitigated);
}

FCombat_DamageResult FCombat_DamageResult::FromContext(const FCombat_DamageContext& Ctx)
{
	FCombat_DamageResult Result;
	Result.Victim = Ctx.Hit.HitActor;
	Result.Instigator = Ctx.Hit.Instigator;

	const float Final = Ctx.ComputeFinalDamage();
	Result.FinalDamage = Final;
	Result.DotDamage = Final * FMath::Clamp(Ctx.DotFraction, 0.f, 1.f);
	Result.PoiseDamage = Ctx.PoiseDamage;
	Result.DamageChannel = Ctx.DamageChannel;
	Result.ImpactPoint = Ctx.Hit.ImpactPoint;

	Result.bIsCritical = Ctx.bIsCritical;
	Result.bIsWeakpoint = Ctx.bIsWeakpoint;
	Result.bWasBlocked = Ctx.bWasBlocked;
	Result.bWasParried = Ctx.bWasParried;
	Result.bWasInvulnerable = Ctx.bWasInvulnerable;
	Result.bStaggered = Ctx.bStaggered;

	// Resolve the single reaction classification with a clear precedence order. The most "interesting"
	// classification wins so listeners (AI threat, audio, hit-react anims) get the salient one.
	if (Ctx.bWasParried)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Parried;
	}
	else if (Ctx.bWasInvulnerable)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Dodged;
	}
	else if (Ctx.bStaggered)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Stagger;
	}
	else if (Ctx.bWasBlocked)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Blocked;
	}
	else if (Ctx.bIsWeakpoint)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Weakpoint;
	}
	else if (Ctx.bIsCritical)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Critical;
	}
	else
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Hit;
	}

	return Result;
}
