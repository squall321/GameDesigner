// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSaveSystemModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"
#include "Storage/SaveX_ContainerHeader.h" // FSaveX_ContainerVersion::Register

namespace SaveXNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_SlotSaved, "DP.Bus.Save.SlotSaved",
		"Broadcast after a named slot is successfully written; payload carries the slot name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_SlotLoaded, "DP.Bus.Save.SlotLoaded",
		"Broadcast after a named slot is successfully loaded; payload carries the slot name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_SlotDeleted, "DP.Bus.Save.SlotDeleted",
		"Broadcast after a named slot is deleted; payload carries the slot name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Autosaved, "DP.Bus.Save.Autosaved",
		"Broadcast after an autosave ring slot is (re)written; payload carries the slot name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Kind, "SaveX.Persist.Kind",
		"Root for SaveSystem-owned ISeam_Persistable record kinds.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Kind_Profile, "SaveX.Persist.Kind.Profile",
		"Root for PROFILE-partition record kinds (cross-save shared data); children are gathered into the profile save.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_StorageRecovered, "DP.Bus.Save.StorageRecovered",
		"Broadcast when a wrapped container is recovered from a backup during load; payload carries the slot name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_CloudConflict, "DP.Bus.Save.CloudConflict",
		"Broadcast when a cloud-vs-local conflict is detected on load; payload carries the slot name.");
}

/**
 * Module implementation for DesignPatternsSaveSystem. Pure lifecycle logging; all behaviour lives
 * in the developer settings, the slot-manager subsystem and the migration step. The core save
 * subsystem (UDP_SaveGameSubsystem) registers the FDP_SaveVersion custom version itself — this
 * module never touches the byte format.
 */
void FDesignPatternsSaveSystemModule::StartupModule()
{
	// Register the wrapper-container custom version (distinct from the core FDP_SaveVersion stream). This is
	// the envelope version only; the inner core blob keeps its own version. Safe to call once at startup.
	FSaveX_ContainerVersion::Register();

	UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystem module started."));
}

void FDesignPatternsSaveSystemModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystem module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsSaveSystemModule, DesignPatternsSaveSystem)
