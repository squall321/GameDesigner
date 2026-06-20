// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/WorldHub_SaveGame.h"
#include "Core/DPLog.h"

void UWorldHub_SaveGame::OnPreSave_Implementation()
{
	// The persistent hub populates Snapshot before handing this object to the save subsystem; nothing
	// to gather here. Keep SaveSchemaVersion current so a future load can branch on it.
	SaveSchemaVersion = CurrentSaveSchemaVersion;
	Snapshot.SnapshotVersion = CurrentSaveSchemaVersion;
}

void UWorldHub_SaveGame::OnPostLoad_Implementation()
{
	// The persistent hub reads Snapshot back via ApplyFromSave; nothing to scatter here directly.
	UE_LOG(LogDPSave, Verbose, TEXT("[WorldHub] SaveGame loaded: schema=%d entries=%d"),
		SaveSchemaVersion, Snapshot.Entries.Num());
}

bool UWorldHub_SaveGame::Migrate_Implementation(int32 FromVersion, int32 ToVersion)
{
	if (!Super::Migrate_Implementation(FromVersion, ToVersion))
	{
		return false;
	}

	// Up-convert the world-hub snapshot schema in place. Currently only v1 exists; future versions
	// add their step here (e.g. renaming a scope field or splitting a value kind).
	if (Snapshot.SnapshotVersion < CurrentSaveSchemaVersion)
	{
		// No structural change yet — just stamp the snapshot to the current schema.
		Snapshot.SnapshotVersion = CurrentSaveSchemaVersion;
	}

	SaveSchemaVersion = CurrentSaveSchemaVersion;
	return true;
}
