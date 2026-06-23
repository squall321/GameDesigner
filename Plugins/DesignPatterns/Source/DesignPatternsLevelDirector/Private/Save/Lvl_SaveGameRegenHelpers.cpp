// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/Lvl_SaveGameRegenHelpers.h"
#include "Save/Lvl_GraphSaveTypes.h"
#include "Core/DPLog.h"

bool FLvl_SaveGameRegenHelpers::IsRecordUsable(const FLvl_GraphSaveRecord& Record)
{
	const bool bHasManifest = Record.Manifest.HasEntries();
	const bool bHasRuleSet = Record.GraphRuleSetTag.IsValid();
	if (!bHasManifest && !bHasRuleSet)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl graph record has neither a manifest nor a rule-set tag; ignored."));
		return false;
	}
	return true;
}

bool FLvl_SaveGameRegenHelpers::ShouldRegenerate(const FLvl_GraphSaveRecord& Record)
{
	return Record.RestoreStrategy == ELvl_RestoreStrategy::RegenerateFromSeed
		&& Record.GraphRuleSetTag.IsValid();
}
