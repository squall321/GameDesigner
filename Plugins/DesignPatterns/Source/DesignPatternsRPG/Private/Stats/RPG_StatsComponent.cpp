// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Stats/RPG_StatsComponent.h"
#include "Core/DPLog.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

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

	for (const FRPG_StatModifier& Mod : Modifiers)
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

void URPG_StatsComponent::RemoveModifiersFromSource(FGameplayTag SourceTag)
{
	// AUTHORITY GUARD: modifiers are granted by server-authoritative sources (equipment/buffs).
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
