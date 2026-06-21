// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSaveSystemModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

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
}

/**
 * Module implementation for DesignPatternsSaveSystem. Pure lifecycle logging; all behaviour lives
 * in the developer settings, the slot-manager subsystem and the migration step. The core save
 * subsystem (UDP_SaveGameSubsystem) registers the FDP_SaveVersion custom version itself — this
 * module never touches the byte format.
 */
void FDesignPatternsSaveSystemModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystem module started."));
}

void FDesignPatternsSaveSystemModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystem module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsSaveSystemModule, DesignPatternsSaveSystem)
