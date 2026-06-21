// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_SaveSlotManager.h"
#include "Save/DPSaveGameSubsystem.h" // EDP_SaveResult + core callback delegate types
#include "SaveX_SlotManagerSubsystem.generated.h"

class UDP_SaveGame;
class UDP_SaveGameSubsystem;
class UDP_ServiceLocatorSubsystem;
class USaveX_DeveloperSettings;

/** Result of a slot-manager save/load/delete request (mirrors and slightly widens EDP_SaveResult). */
UENUM(BlueprintType)
enum class ESaveX_SlotResult : uint8
{
	/** The operation succeeded. */
	Success,
	/** A passed-in argument was invalid (empty slot name, etc.). */
	InvalidArgument,
	/** Creating a new named slot would exceed USaveX_DeveloperSettings::MaxNamedSlots. */
	SlotLimitReached,
	/** The requested slot does not exist on disk. */
	SlotNotFound,
	/** The core save subsystem could not be resolved (no GameInstance / early load). */
	SubsystemUnavailable,
	/** The core save/load/delete itself failed (see the log for the underlying EDP_SaveResult). */
	CoreFailure,
	/** A newer request for the same slot arrived before this one completed; this callback is retired. */
	Superseded
};

/** Per-call native completion callback for a slot save. */
DECLARE_DELEGATE_TwoParams(FSaveX_SlotSaveDone, const FString& /*Slot*/, ESaveX_SlotResult /*Result*/);

/** Per-call native completion callback for a slot load; SaveObject is null unless Result==Success. */
DECLARE_DELEGATE_ThreeParams(FSaveX_SlotLoadDone, const FString& /*Slot*/, ESaveX_SlotResult /*Result*/, UDP_SaveGame* /*SaveObject*/);

/** Lightweight bus payload carrying the affected slot name for SaveX bus channels. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSAVESYSTEM_API FSaveX_SlotBusPayload
{
	GENERATED_BODY()

	/** The slot the bus event refers to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FString SlotName;
};

/** Broadcast (game thread) after any slot write completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnSlotSaved, FString, Slot, bool, bSuccess);

/** Broadcast (game thread) after any slot load completes; SaveObject null unless bSuccess. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSaveX_OnSlotLoaded, FString, Slot, bool, bSuccess, UDP_SaveGame*, SaveObject);

/**
 * Named-slot POLICY layer over the core UDP_SaveGameSubsystem and the implementation of the shared
 * ISeam_SaveSlotManager seam.
 *
 * RESPONSIBILITIES (policy only — it reinvents NO serialization):
 *  - Slot bookkeeping: named-slot cap enforcement, autosave ring rotation, a reserved "continue"
 *    slot, and "most-recent" resolution via the core ReadSlotHeader timestamps.
 *  - Gather: on save it constructs the project's UDP_SaveGame subclass, sets DisplayName /
 *    PlaytimeSeconds, gathers every live ISeam_Persistable participant (weak-pruned, authority-
 *    guarded) into the save object's records, then hands the object to the core subsystem's
 *    SaveAsync — which owns the byte/header/async-IO.
 *  - Metadata: ListSlots reads each slot's header (core ReadSlotHeader) into FSeam_SaveSlotInfo so a
 *    save/load UI and a "Continue" button work without inventing their own bookkeeping.
 *  - Migration: Initialize() registers the SaveSystem migration step(s) via the core
 *    UDP_SaveGameSubsystem::GetMigration()->RegisterStepClass (NOT module startup — no GameInstance
 *    exists there).
 *
 * It registers itself in the service locator under USaveX_DeveloperSettings::SlotManagerServiceTag
 * (default SaveX_ServiceKeys::SlotManager()) so consumers resolve the seam without hard-coupling.
 *
 * REPLICATION: this is a subsystem and holds NO replicated state. RestoreState on participants is
 * authority-guarded by the ISeam_Persistable contract; the gather here additionally authority-guards
 * so a client-side save never reads server-authoritative state it does not own.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_SlotManagerSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_SaveSlotManager
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Named-slot operations ----

	/**
	 * Create (or overwrite) and write a named slot. Enforces the named-slot cap for a NEW slot.
	 * Gathers persistable participants and routes to the core SaveAsync.
	 *
	 * @param SlotName     Target slot (must be non-empty). Reserved autosave/continue names are rejected.
	 * @param DisplayName  Player-facing label written into the save header. If empty, SlotName is used.
	 * @param OnDone       Optional native completion callback (game thread).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void CreateSlot(const FString& SlotName, const FString& DisplayName);

	/** Native variant of CreateSlot with a completion callback. */
	void SaveToSlot(const FString& SlotName, const FString& DisplayName, FSaveX_SlotSaveDone OnDone = FSaveX_SlotSaveDone());

	/**
	 * Load a named slot, run the core deserialize+migrate, then (on the game thread) scatter the
	 * restored state back into the world via ISeam_Persistable::RestoreState on matching participants.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void LoadSlot(const FString& SlotName);

	/** Native variant of LoadSlot with a completion callback. */
	void LoadSlotWithCallback(const FString& SlotName, FSaveX_SlotLoadDone OnDone);

	/** Delete a named slot's file. Broadcasts the deleted bus channel + OnSlotSaved is NOT fired. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DeleteSlot(const FString& SlotName);

	/**
	 * Rename a slot's player-facing label. Because the label lives inside the save header (not in the
	 * file name), this loads the slot, updates DisplayName, and re-saves under the same file name.
	 * Returns false synchronously if the slot is missing or the core subsystem is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool RenameSlot(const FString& SlotName, const FString& NewDisplayName);

	/** Enumerate all known slots (named + ring + continue) as FSeam_SaveSlotInfo, via core headers. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void ListSlots(TArray<FSeam_SaveSlotInfo>& OutSlots) const;

	/** Write the next autosave ring slot (rotating), subject to the configured min-interval throttle. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void RequestAutosave();

	/** Write the reserved "continue" slot (always overwrites it). Backs a quick-save / continue flow. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void SaveContinue(const FString& DisplayName);

	/** Resolve the slot a "Continue" button should load: the most-recent of {continue, ring, named}. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	FString GetContinueSlot() const;

	// ---- ISeam_SaveSlotManager ----

	/** Seam: metadata for every known slot. */
	virtual void GetAllSlots_Implementation(TArray<FSeam_SaveSlotInfo>& OutSlots) const override;

	/** Seam: the most-recently-written slot name (empty if none). */
	virtual FString GetMostRecentSlot_Implementation() const override;

	/** Seam: true if a slot with this name exists on disk. */
	virtual bool DoesSlotExist_Implementation(const FString& SlotName) const override;

	// ---- Delegates ----

	/** Broadcast after any slot write completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FSaveX_OnSlotSaved OnSlotSaved;

	/** Broadcast after any slot load completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FSaveX_OnSlotLoaded OnSlotLoaded;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Effective settings CDO (never null after engine init; callers still null-check). */
	const USaveX_DeveloperSettings* GetSettings() const;

	/** Resolve the core save subsystem (the byte/header/async-IO owner). Null in early-load contexts. */
	UDP_SaveGameSubsystem* GetCoreSaveSubsystem() const;

	/** Resolve the service locator (for seam registration / continue resolution). */
	UDP_ServiceLocatorSubsystem* GetServiceLocator() const;

	/**
	 * GAME-THREAD: build a save object of the configured class, set DisplayName / PlaytimeSeconds, and
	 * gather every live ISeam_Persistable participant into it (weak-pruned, authority-guarded). Returns
	 * null only if a save object could not be constructed.
	 */
	UDP_SaveGame* BuildSaveObjectForWrite(const FString& DisplayName) const;

	/**
	 * GAME-THREAD: enumerate every live ISeam_Persistable participant (actors + their components) in the
	 * GameInstance's world, authority-guarded (no gather on a pure client) and weak/validity-pruned, and
	 * deposit each captured record into SaveObject when it aggregates records (implements ISeam_Persistable).
	 */
	void GatherParticipantsInto(UDP_SaveGame* SaveObject) const;

	/** Shared write path: validate, build, hand to core SaveAsync, broadcast + invoke OnDone on completion. */
	void InternalSave(const FString& SlotName, const FString& DisplayName, bool bIsAutosave, FSaveX_SlotSaveDone OnDone);

	/** Shared load path: core LoadAsync, then scatter to participants and broadcast + invoke OnDone. */
	void InternalLoad(const FString& SlotName, FSaveX_SlotLoadDone OnDone);

	/** Scatter a loaded save object back into the world via participant RestoreState (authority by contract). */
	void ScatterToParticipants(UDP_SaveGame* Loaded) const;

	/** True if SlotName is a reserved (ring or continue) name that CreateSlot must not target. */
	bool IsReservedSlotName(const FString& SlotName) const;

	/** Count of existing NAMED slots (excludes ring + continue), for the cap check. */
	int32 CountNamedSlots() const;

	/** Compute the file name of the next ring slot to overwrite (oldest by header timestamp). */
	FString ComputeNextAutosaveSlot() const;

	/** Map a core EDP_SaveResult to the slot-manager result enum. */
	static ESaveX_SlotResult FromCoreResult(EDP_SaveResult CoreResult);

	/** Broadcast a slot bus message (channel + slot-name payload) if a bus is available. */
	void BroadcastSlotBus(FGameplayTag Channel, const FString& SlotName) const;

	/** Map a service-locator tag from settings, falling back to the conventional key if invalid. */
	FGameplayTag ResolveServiceKey() const;

	/**
	 * UFUNCTION bound to the core SaveAsync per-call dynamic callback. Looks up the pending write by
	 * slot, broadcasts, invokes the stored native OnDone, and updates "most recent"/throttle state.
	 */
	UFUNCTION()
	void HandleCoreSaveComplete(FString Slot, EDP_SaveResult Result);

	/** UFUNCTION bound to the core LoadAsync per-call dynamic callback; scatters then broadcasts. */
	UFUNCTION()
	void HandleCoreLoadComplete(FString Slot, EDP_SaveResult Result, UDP_SaveGame* SaveObject);

	/** A queued write awaiting the core async callback (keyed by slot in PendingSaves). */
	struct FPendingSave
	{
		bool bIsAutosave = false;
		FSaveX_SlotSaveDone OnDone;
	};

	/** A queued load awaiting the core async callback (keyed by slot in PendingLoads). */
	struct FPendingLoad
	{
		FSaveX_SlotLoadDone OnDone;
	};

	/** In-flight writes by slot name (native callbacks survive across the async boundary here). */
	TMap<FString, FPendingSave> PendingSaves;

	/** In-flight loads by slot name. */
	TMap<FString, FPendingLoad> PendingLoads;

	/** Tag this manager registered under (cached so Deinitialize can unregister exactly that key). */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceKey;

	/** Monotonic clock seconds of the last autosave, for the min-interval throttle (<0 = never). */
	double LastAutosaveRealTimeSeconds = -1.0;

	/** Count of in-flight async save/load ops, surfaced in the debug string. */
	int32 PendingOps = 0;
};
