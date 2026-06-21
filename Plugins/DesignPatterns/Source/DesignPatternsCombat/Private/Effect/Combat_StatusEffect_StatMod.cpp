// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Effect/Combat_StatusEffect_StatMod.h"
#include "Combat_DeepNativeTags.h"

#include "Stats/Seam_StatModifierSink.h"

#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

FGameplayTag UCombat_StatusEffect_StatMod::ResolveSourceTag() const
{
	if (ModifierSourceTag.IsValid())
	{
		return ModifierSourceTag;
	}
	if (EffectTag.IsValid())
	{
		return EffectTag;
	}
	return CombatDeepNativeTags::StatSource_Status; // documented fallback
}

void UCombat_StatusEffect_StatMod::ContributeDerivedModifiers(AActor* Target, bool bApply) const
{
	if (!Target)
	{
		return;
	}

	// Resolve the stat sink via the seam off the target actor. Cross-module coupling stays seam-only.
	// The sink may be implemented on the actor itself or on one of its components (e.g. a stats comp).
	UObject* Sink = nullptr;
	if (Target->GetClass()->ImplementsInterface(USeam_StatModifierSink::StaticClass()))
	{
		Sink = Target;
	}
	else if (UActorComponent* Comp = Target->FindComponentByInterface(USeam_StatModifierSink::StaticClass()))
	{
		Sink = Comp;
	}

	if (!Sink)
	{
		return;
	}

	const FGameplayTag Source = ResolveSourceTag();

	// Build the group, stamping our source key onto each modifier so the sink groups them together.
	TArray<FSeam_StatMod> Group;
	if (bApply)
	{
		Group.Reserve(Modifiers.Num());
		for (FSeam_StatMod Mod : Modifiers)
		{
			Mod.SourceTag = Source;
			Group.Add(Mod);
		}
	}

	// DUAL-PATH STAT RULE: status effects are LOCAL-DERIVED from replicated state, so we ALWAYS use the
	// SetDerivedModifierGroup path (server AND clients, no authority guard). An empty array on remove
	// clears the group. NEVER AddModifierBatch here.
	ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(Sink, Source, Group);

	UE_LOG(LogDP, Verbose, TEXT("[StatusStatMod] %s derived group '%s' (%d mods) on %s."),
		bApply ? TEXT("set") : TEXT("cleared"), *Source.ToString(), Group.Num(), *GetNameSafe(Target));
}

void UCombat_StatusEffect_StatMod::OnApply_Implementation(AActor* Target)
{
	Super::OnApply_Implementation(Target);
	ContributeDerivedModifiers(Target, /*bApply*/ true);
}

void UCombat_StatusEffect_StatMod::OnRemove_Implementation(AActor* Target, bool bExpiredNaturally)
{
	ContributeDerivedModifiers(Target, /*bApply*/ false);
	Super::OnRemove_Implementation(Target, bExpiredNaturally);
}
