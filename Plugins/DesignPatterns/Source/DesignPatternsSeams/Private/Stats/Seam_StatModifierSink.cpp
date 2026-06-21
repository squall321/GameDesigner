// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Stats/Seam_StatModifierSink.h"

// Default (unoverridden) implementations: an object that does not implement a real stat sink is a no-op.
// Real implementers (URPG_StatsComponent) override all three. Fail-closed: an absent sink silently drops
// contributions rather than asserting, so a project without an RPG stats component still runs.

void ISeam_StatModifierSink::AddModifierBatch_Implementation(FGameplayTag /*SourceTag*/, const TArray<FSeam_StatMod>& /*Mods*/)
{
}

void ISeam_StatModifierSink::RemoveModifiersFromSource_Implementation(FGameplayTag /*SourceTag*/)
{
}

void ISeam_StatModifierSink::SetDerivedModifierGroup_Implementation(FGameplayTag /*SourceTag*/, const TArray<FSeam_StatMod>& /*Mods*/)
{
}
