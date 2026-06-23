// Copyright DesignPatterns plugin. All Rights Reserved.

#include "AI/Seam_EncounterDirector.h"

// INERT native defaults for the encounter-director seam. A project with no AI spawn director leaves
// these unoverridden, so the pacing producer's calls become harmless no-ops (activation refused, no
// encounter ever active). The real AI-side adapter overrides all three to forward to the concrete
// UAI_SpawnDirectorSubsystem after mapping the EncounterId tag to its encounter data asset.

bool ISeam_EncounterDirector::ActivateEncounterForRegion_Implementation(
	FGameplayTag /*RegionTag*/, FGameplayTag /*EncounterId*/, float /*ProgressionInput*/)
{
	return false;
}

bool ISeam_EncounterDirector::StopEncounter_Implementation(FGameplayTag /*RegionTag*/)
{
	return false;
}

bool ISeam_EncounterDirector::IsEncounterActive_Implementation() const
{
	return false;
}
