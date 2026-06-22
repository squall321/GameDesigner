// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * SaveSystem module: a thin, designer-facing NAMED-SLOT policy layer over the core
 * UDP_SaveGameSubsystem. It owns slot bookkeeping ("most-recent" / "continue"), gathers the
 * game's ISeam_Persistable participants into a save object, registers version migration steps,
 * and exposes the whole thing through the shared ISeam_SaveSlotManager seam.
 *
 * It deliberately reinvents NONE of the byte/header/async-IO machinery: every actual read/write
 * delegates to the core subsystem. This module is purely the "which slot, what metadata, who
 * participates" policy.
 */
class FDesignPatternsSaveSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

/**
 * Native (C++-defined) anchor tags for the SaveSystem module.
 *
 * Roots:
 *  - SaveX.*          : module-owned roots (record kinds).
 *  - DP.Bus.Save.*    : message-bus channels the slot manager broadcasts on (anchored under the
 *                       core DP.Bus root).
 *
 * The service-locator KEY under which the slot manager publishes its ISeam_SaveSlotManager adapter
 * is owned by SaveX_ServiceKeys::SlotManager() (SaveX_ServiceKeys.h) so the checkpoint/autosave area
 * and the save/load UI resolve the same key. It is intentionally NOT re-declared here.
 *
 * Full tag strings + comments live in DesignPatternsSaveSystemModule.cpp.
 */
namespace SaveXNativeTags
{
	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast after a slot is successfully written (payload carries slot name). */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SlotSaved);

	/** Bus channel: broadcast after a slot is successfully loaded (payload carries slot name). */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SlotLoaded);

	/** Bus channel: broadcast after a slot is deleted (payload carries slot name). */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SlotDeleted);

	/** Bus channel: broadcast when an autosave ring slot is (re)written (payload carries slot name). */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Autosaved);

	// --- Persistence record kinds (SaveX root) ---

	/** Root for all SaveSystem-owned persistence record kinds (ISeam_Persistable::GetPersistenceKind). */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Kind);

	/** Root for the PROFILE partition record kinds (cross-save shared data). Children belong to the profile. */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Kind_Profile);

	// --- Storage-layer bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast when a wrapped container is recovered from a backup during load. */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_StorageRecovered);

	/** Bus channel: broadcast when a cloud-vs-local conflict is detected on load. */
	DESIGNPATTERNSSAVESYSTEM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_CloudConflict);
}
