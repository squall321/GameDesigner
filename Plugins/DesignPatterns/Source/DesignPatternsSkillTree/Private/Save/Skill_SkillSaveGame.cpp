// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/Skill_SkillSaveGame.h"
#include "Component/Skill_SkillComponent.h"
#include "Core/DPLog.h"

bool USkill_SkillSaveGame::CaptureFrom(USkill_SkillComponent* Component)
{
	if (!Component)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] CaptureFrom: null component, nothing captured."));
		return false;
	}

	// Pure gather (no authority needed): copy the live learned records and earned-point total into this save
	// object. Reads replicated state, so this is valid on server or client.
	SavedSkills = Component->GetLearnedSkills();
	SavedPoints = Component->GetTotalEarnedPoints();

	UE_LOG(LogDP, Log, TEXT("[SkillTree] Captured %d learned skills, %d earned points into save."),
		SavedSkills.Num(), SavedPoints);
	return true;
}

bool USkill_SkillSaveGame::RestoreInto(USkill_SkillComponent* Component) const
{
	if (!Component)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] RestoreInto: null component, nothing restored."));
		return false;
	}

	// Authority-guarded scatter: ImportFromSave itself guards HasAuthority() and is a documented no-op on
	// clients (they receive the restored state via replication). We forward unconditionally and let the
	// component enforce the authority contract in one place.
	Component->ImportFromSave(SavedSkills, SavedPoints);
	return true;
}
