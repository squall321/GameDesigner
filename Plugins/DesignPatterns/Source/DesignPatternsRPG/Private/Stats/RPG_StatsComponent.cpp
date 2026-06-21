// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Stats/RPG_StatsComponent.h"
#include "Stats/Seam_StatMod.h"
#include "Core/DPLog.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

namespace
{
	/** Map a seam-neutral modifier to the RPG concrete modifier (Op is the integer ERPG_StatModOp). */
	FRPG_StatModifier SeamModToRPG(const FSeam_StatMod& In, const FGameplayTag& SourceTag)
	{
		FRPG_StatModifier Mod;
		Mod.AttributeTag = In.AttributeTag;
		Mod.Op = static_cast<ERPG_StatModOp>(In.Op);
		Mod.Magnitude = (In.Magnitude.Type == ESeam_NetValueType::Float)
			? static_cast<float>(In.Magnitude.FloatValue)
			: 0.f;
		Mod.SourceTag = SourceTag;
		return Mod;
	}
}

URPG_StatsComponent::URPG_StatsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPG_StatsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_StatsComponent, Level);
	DOREPLIFETIME(URPG_StatsComponent, CurrentXP);
}

void URPG_StatsComponent::SetBaseAttribute(FGameplayTag AttributeTag, float Value)
{
	// AUTHORITY GUARD: base attributes feed server-side derived stats.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!AttributeTag.IsValid())
	{
		return;
	}
	BaseAttributes.FindOrAdd(AttributeTag) = Value;
	NotifyAttributeChanged(AttributeTag);
}

float URPG_StatsComponent::GetBaseAttribute(FGameplayTag AttributeTag) const
{
	const float* Found = BaseAttributes.Find(AttributeTag);
	return Found ? *Found : 0.f;
}

float URPG_StatsComponent::ComputeDerived(const FGameplayTag& AttributeTag) const
{
	float Value = GetBaseAttribute(AttributeTag);
	float MultiplierAccum = 1.f;
	bool bHasOverride = false;
	float OverrideValue = 0.f;

	// Fold BOTH the authority-granted Modifiers and the locally-derived DerivedModifiers (equipment/affix/
	// set/encumbrance/status). Both contribute identically; the separation only exists so the derived path
	// can run without an authority guard, never funnelling through the authority-only AddModifier.
	auto FoldList = [&](const TArray<FRPG_StatModifier>& List)
	{
		for (const FRPG_StatModifier& Mod : List)
		{
			if (Mod.AttributeTag != AttributeTag)
			{
				continue;
			}
			switch (Mod.Op)
			{
			case ERPG_StatModOp::Additive:
				Value += Mod.Magnitude;
				break;
			case ERPG_StatModOp::Multiplicative:
				MultiplierAccum *= (1.f + Mod.Magnitude);
				break;
			case ERPG_StatModOp::Override:
				bHasOverride = true;
				OverrideValue = Mod.Magnitude;
				break;
			default:
				break;
			}
		}
	};
	FoldList(Modifiers);
	FoldList(DerivedModifiers);

	if (bHasOverride)
	{
		return OverrideValue;
	}
	return Value * MultiplierAccum;
}

float URPG_StatsComponent::GetAttributeValue(FGameplayTag AttributeTag) const
{
	return ComputeDerived(AttributeTag);
}

void URPG_StatsComponent::NotifyAttributeChanged(const FGameplayTag& AttributeTag)
{
	OnStatChanged.Broadcast(this, AttributeTag, ComputeDerived(AttributeTag));
}

void URPG_StatsComponent::AddModifier(const FRPG_StatModifier& Modifier)
{
	// AUTHORITY GUARD: modifiers feed derived stats used in gameplay logic, so they must be granted
	// server-side (a client adding its own modifier would be a cheat). Matches SetBaseAttribute/AddXP.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!Modifier.AttributeTag.IsValid())
	{
		return;
	}
	Modifiers.Add(Modifier);
	NotifyAttributeChanged(Modifier.AttributeTag);
}

void URPG_StatsComponent::RemoveModifiersFromSource_Implementation(FGameplayTag SourceTag)
{
	// AUTHORITY GUARD: the authority-granted Modifiers list is server-owned. (This is the seam's
	// authority-only removal path, and also the original public RemoveModifiersFromSource entry point.)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!SourceTag.IsValid())
	{
		return;
	}

	// Collect affected attributes so we can re-notify exactly those.
	TSet<FGameplayTag> Affected;
	for (const FRPG_StatModifier& Mod : Modifiers)
	{
		if (Mod.SourceTag == SourceTag)
		{
			Affected.Add(Mod.AttributeTag);
		}
	}

	Modifiers.RemoveAll([&SourceTag](const FRPG_StatModifier& Mod)
	{
		return Mod.SourceTag == SourceTag;
	});

	for (const FGameplayTag& AttributeTag : Affected)
	{
		NotifyAttributeChanged(AttributeTag);
	}
}

void URPG_StatsComponent::AddModifierBatch_Implementation(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods)
{
	// AUTHORITY GUARD: gameplay-granted buffs are server-authoritative (matches AddModifier).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!SourceTag.IsValid())
	{
		return;
	}

	// Replace any existing authority batch under this source, then add the new one.
	Execute_RemoveModifiersFromSource(this, SourceTag);

	TSet<FGameplayTag> Affected;
	for (const FSeam_StatMod& SeamMod : Mods)
	{
		const FRPG_StatModifier Mod = SeamModToRPG(SeamMod, SourceTag);
		if (!Mod.AttributeTag.IsValid())
		{
			continue;
		}
		Modifiers.Add(Mod);
		Affected.Add(Mod.AttributeTag);
	}
	for (const FGameplayTag& AttributeTag : Affected)
	{
		NotifyAttributeChanged(AttributeTag);
	}
}

void URPG_StatsComponent::SetDerivedModifierGroup_Implementation(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods)
{
	// NO AUTHORITY GUARD: this is pure local derivation from already-replicated state and MUST run on
	// server AND clients, or equipment/affix/set/encumbrance/status modifiers would silently desync.
	if (!SourceTag.IsValid())
	{
		return;
	}

	// Gather attributes affected by the OUTGOING group (so removals re-notify even if the new group is empty).
	TSet<FGameplayTag> Affected;
	for (const FRPG_StatModifier& Mod : DerivedModifiers)
	{
		if (Mod.SourceTag == SourceTag)
		{
			Affected.Add(Mod.AttributeTag);
		}
	}

	// Replace the whole group for this source atomically.
	DerivedModifiers.RemoveAll([&SourceTag](const FRPG_StatModifier& Mod)
	{
		return Mod.SourceTag == SourceTag;
	});
	for (const FSeam_StatMod& SeamMod : Mods)
	{
		const FRPG_StatModifier Mod = SeamModToRPG(SeamMod, SourceTag);
		if (!Mod.AttributeTag.IsValid())
		{
			continue;
		}
		DerivedModifiers.Add(Mod);
		Affected.Add(Mod.AttributeTag);
	}

	for (const FGameplayTag& AttributeTag : Affected)
	{
		NotifyAttributeChanged(AttributeTag);
	}
}

float URPG_StatsComponent::GetXPToNextLevel(int32 ForLevel) const
{
	const int32 ClampedLevel = FMath::Max(1, ForLevel);
	if (XPCurve)
	{
		return FMath::Max(1.f, XPCurve->GetFloatValue(static_cast<float>(ClampedLevel)));
	}
	// Quadratic fallback: 100 * level^2.
	return 100.f * static_cast<float>(ClampedLevel) * static_cast<float>(ClampedLevel);
}

void URPG_StatsComponent::AddXP(float Amount)
{
	// AUTHORITY GUARD: level/XP are replicated, server-authoritative state.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (Amount <= 0.f)
	{
		return;
	}

	const int32 OldLevel = Level;
	CurrentXP += Amount;

	// Roll over into level-ups while enough XP has accumulated.
	float Required = GetXPToNextLevel(Level);
	while (CurrentXP >= Required && Required > 0.f)
	{
		CurrentXP -= Required;
		++Level;
		Required = GetXPToNextLevel(Level);
	}

	if (Level > OldLevel)
	{
		// Server-side broadcast; clients receive it through OnRep_Level.
		for (int32 NewLevel = OldLevel + 1; NewLevel <= Level; ++NewLevel)
		{
			OnLevelUp.Broadcast(this, NewLevel);
		}
		UE_LOG(LogDPData, Verbose, TEXT("[RPG_Stats] Leveled %d -> %d"), OldLevel, Level);
	}
}

void URPG_StatsComponent::SetLevel(int32 NewLevel)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const int32 Clamped = FMath::Max(1, NewLevel);
	const int32 OldLevel = Level;
	Level = Clamped;
	CurrentXP = 0.f;

	if (Level > OldLevel)
	{
		for (int32 L = OldLevel + 1; L <= Level; ++L)
		{
			OnLevelUp.Broadcast(this, L);
		}
	}
}

void URPG_StatsComponent::OnRep_Level(int32 OldLevel)
{
	if (Level > OldLevel)
	{
		for (int32 L = OldLevel + 1; L <= Level; ++L)
		{
			OnLevelUp.Broadcast(this, L);
		}
	}
}

void URPG_StatsComponent::OnRep_CurrentXP()
{
	// Hook for UI refresh; no authoritative side effects on the client.
}
