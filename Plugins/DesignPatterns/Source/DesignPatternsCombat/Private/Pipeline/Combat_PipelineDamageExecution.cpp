// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_PipelineDamageExecution.h"
#include "Pipeline/Combat_DamageModifier.h"
#include "Defense/Combat_DefenseComponent.h"
#include "Hit/Combat_WeakpointComponent.h"
#include "Combat_DeepNativeTags.h"

#include "Action/DPGameplayActionComponent.h"

#include "GameFramework/Actor.h"

float UCombat_PipelineDamageExecution::CalculateDamage_Implementation(const FCombat_HitResult& Hit) const
{
	FCombat_DamageContext Context;
	BuildContextAndRun(Hit, Context);
	return Context.ComputeFinalDamage();
}

void UCombat_PipelineDamageExecution::BuildContextAndRun(const FCombat_HitResult& Hit, FCombat_DamageContext& OutContext) const
{
	OutContext = FCombat_DamageContext::FromHit(Hit);

	// Resolve the impacted bone if the hit carried one (the legacy FCombat_HitResult has no bone, so
	// this stays NAME_None unless a subclass/feature sets it; weakpoint then no-ops gracefully).
	// The pipeline component may pre-set OutContext.HitBoneName before calling; here we only read it.

	RunModifiers(OutContext);
	if (bUseDefenseMitigation)
	{
		RunPureMitigation(OutContext);
	}
}

void UCombat_PipelineDamageExecution::RunModifiers(FCombat_DamageContext& Context) const
{
	// Build a stable, priority-ordered view without mutating the authored array order.
	TArray<const UCombat_DamageModifier*> Ordered;
	Ordered.Reserve(Modifiers.Num());
	for (const TObjectPtr<UCombat_DamageModifier>& Mod : Modifiers)
	{
		if (Mod)
		{
			Ordered.Add(Mod.Get());
		}
	}
	Ordered.StableSort([](const UCombat_DamageModifier& A, const UCombat_DamageModifier& B)
	{
		return A.Priority < B.Priority;
	});

	for (const UCombat_DamageModifier* Mod : Ordered)
	{
		if (Mod && Mod->MatchesChannel(Context))
		{
			Mod->Modify(Context);
		}
	}
}

void UCombat_PipelineDamageExecution::RunPureMitigation(FCombat_DamageContext& Context) const
{
	const AActor* Victim = Context.Hit.HitActor.Get();
	if (!Victim)
	{
		return;
	}

	// --- Weakpoint (read-only): fold the zone multiplier into the running multiplier + set the flag.
	if (const UCombat_WeakpointComponent* Weak = Victim->FindComponentByClass<UCombat_WeakpointComponent>())
	{
		float ZoneMult = 1.f;
		FGameplayTag ZoneTag;
		bool bIsWeak = false;
		if (Weak->QueryZone(Context.HitBoneName, ZoneMult, ZoneTag, bIsWeak))
		{
			Context.MultiplierFactor *= FMath::Max(0.f, ZoneMult);
			Context.bIsWeakpoint = Context.bIsWeakpoint || bIsWeak;
		}
	}

	// --- I-frames (read-only): the shared owned-tag negates the hit entirely.
	if (const UDP_GameplayActionComponent* Action = Victim->FindComponentByClass<UDP_GameplayActionComponent>())
	{
		if (Action->GetOwnedTags().HasTag(CombatDeepNativeTags::Status_IFrame))
		{
			Context.bWasInvulnerable = true;
		}
	}

	// --- Defense (read-only): block chip / parry / dodge invuln.
	if (const UCombat_DefenseComponent* Defense = Victim->FindComponentByClass<UCombat_DefenseComponent>())
	{
		float ChipFraction = 1.f;
		bool bInvuln = false;
		bool bParry = false;
		if (Defense->QueryIncoming(Context.Hit, ChipFraction, bInvuln, bParry))
		{
			Context.bWasParried = Context.bWasParried || bParry;
			Context.bWasInvulnerable = Context.bWasInvulnerable || bInvuln;
			if (!bParry && !bInvuln)
			{
				Context.bWasBlocked = true;
				// A block scales the surviving damage by the chip fraction (folds into the multiplier).
				Context.MultiplierFactor *= FMath::Clamp(ChipFraction, 0.f, 1.f);
			}
		}
	}
}
