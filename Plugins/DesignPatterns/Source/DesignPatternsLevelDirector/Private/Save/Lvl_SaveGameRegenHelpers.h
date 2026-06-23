// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FLvl_GraphSaveRecord;
struct FLvl_PlacementManifest;

/**
 * Free/static helpers for restoring dungeon-graph save records WITHOUT touching ULvl_SaveGame's
 * existing RestoreInto signature (its single Placement-kind contract is left intact).
 *
 * These are intentionally header-light (forward-declared records) and live in the Private folder: they
 * are an implementation detail shared by ULvl_GraphGeneratorComponent's ISeam_Persistable path.
 */
struct FLvl_SaveGameRegenHelpers
{
	/**
	 * Validate a graph save record before it is consumed: a usable record must name a region, carry a
	 * seed, and either carry a manifest (for verbatim) or a rule-set tag (for regenerate). Returns true
	 * if the record is usable; logs and returns false otherwise.
	 */
	static bool IsRecordUsable(const FLvl_GraphSaveRecord& Record);

	/** True if the record asks to regenerate from seed AND has a rule-set tag to regenerate from. */
	static bool ShouldRegenerate(const FLvl_GraphSaveRecord& Record);
};
