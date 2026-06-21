// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Migration/SaveX_MigrationStep.h"
#include "Save/DPSaveGame.h"
#include "Core/DPLog.h"

USaveX_MigrationStep::USaveX_MigrationStep()
{
	// All tunables default in the header; nothing to construct.
}

int32 USaveX_MigrationStep::GetFromVersion_Implementation() const
{
	return static_cast<int32>(FDP_SaveVersion::InitialVersion);
}

int32 USaveX_MigrationStep::GetToVersion_Implementation() const
{
	return static_cast<int32>(FDP_SaveVersion::AddedSlotMetadata);
}

bool USaveX_MigrationStep::Apply_Implementation(UDP_SaveGame* Save) const
{
	if (!Save)
	{
		// A null save is a registry/caller bug, not a migration failure of this step. Log and
		// succeed so the chain is not aborted by an unrelated problem.
		UE_LOG(LogDPSave, Warning,
			TEXT("SaveX_MigrationStep::Apply called with a null save object; skipping (no-op)."));
		return true;
	}

	// Idempotent: only fill a DisplayName that is still empty (a re-run, or a save that already
	// carried metadata, leaves the existing label untouched). PlaytimeSeconds keeps its deserialized
	// default of 0 — there is no source for historical playtime in a pre-metadata blob.
	if (Save->DisplayName.IsEmpty())
	{
		Save->DisplayName = BackfilledDisplayName.IsEmpty() ? TEXT("Recovered Save") : BackfilledDisplayName;
		UE_LOG(LogDPSave, Verbose,
			TEXT("SaveX_MigrationStep: backfilled DisplayName '%s' on a pre-metadata save (v%d -> v%d)."),
			*Save->DisplayName, GetFromVersion_Implementation(), GetToVersion_Implementation());
	}

	return true;
}
