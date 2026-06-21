// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "UObject/ScriptInterface.h"
#include "SaveX_AutosaveSubsystem.generated.h"

class ISeam_SaveSlotManager;
class UDP_SaveGameSubsystem;
class UDP_SaveGame;
class UDP_MessageBusSubsystem;

/** Why an autosave was requested — surfaced in logs and the debug string, and usable for policy. */
UENUM(BlueprintType)
enum class ESaveX_AutosaveReason : uint8
{
	/** The periodic interval ticker elapsed. */
	Interval,
	/** A subscribed message-bus channel fired. */
	BusEvent,
	/** A checkpoint was recorded. */
	Checkpoint,
	/** An explicit Blueprint/C++ caller asked for an autosave. */
	Manual
};

/** Fired (game thread) when an autosave write completes, with the ring slot and success flag. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnAutosaveCompleted, FString, Slot, bool, bSuccess);

/**
 * GameInstance-scoped driver of a throttled, ROTATING autosave ring.
 *
 * Responsibilities:
 *  - Triggers: a periodic interval (FTSTicker, removed on Deinitialize), subscribed message-bus channels,
 *    and checkpoint records. All triggers funnel through RequestAutosave, which is throttled by a minimum
 *    interval so a burst of triggers collapses to a single disk write.
 *  - Ring: writes cycle across N slots ("<Prefix>_<index>") so an autosave never clobbers the only recent
 *    save. Ring size and cadence come from USaveX_DeveloperSettings.
 *  - WRAPS the core UDP_SaveGameSubsystem for the actual byte/header/IO work; it invents no serialization.
 *  - Uses the ISeam_SaveSlotManager seam only to read slot metadata (e.g. to pick the next/oldest ring slot
 *    deterministically when the in-memory cursor is unknown, such as right after level travel). When no slot
 *    manager is registered it degrades to a simple monotonic cursor.
 *
 * This is a SUBSYSTEM: it holds NO replicated state. The save object it builds is a transient gather of
 * already-replicated/local state; only the server's save subsystem performs the persistence on a listen
 * server, while clients simply skip the write (guarded by world authority where relevant via the host).
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_AutosaveSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Request an autosave for the given reason. Subject to the min-interval throttle: if the previous write
	 * was too recent the request is dropped (logged at Verbose) and false is returned. Otherwise the next
	 * ring slot is written via the wrapped save subsystem and true is returned.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Autosave")
	bool RequestAutosave(ESaveX_AutosaveReason Reason);

	/** True if an autosave could run right now (i.e. the throttle window has elapsed and a backend exists). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Autosave")
	bool CanAutosaveNow() const;

	/** The ring slot name that the next autosave will write to (current cursor). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Autosave")
	FString GetNextRingSlotName() const;

	/** Enumerate all ring slot names for this project's ring size (e.g. for a UI or debug command). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Autosave")
	void GetRingSlotNames(TArray<FString>& OutSlots) const;

	/** Fired when an autosave write completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Autosave")
	FSaveX_OnAutosaveCompleted OnAutosaveCompleted;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/**
	 * Cached message bus (GI-scoped sibling subsystem; shares this subsystem's GameInstance lifetime, so
	 * a strong transient ref is correct — matches the Analytics_SessionTracker convention). Re-resolved on
	 * Initialize; never accessed after Deinitialize.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UDP_MessageBusSubsystem> MessageBus = nullptr;

	/** FTSTicker handle for the periodic interval driver; removed on Deinitialize. */
	FTSTicker::FDelegateHandle IntervalTickerHandle;

	/** Bus listener handles we own, removed on Deinitialize. */
	TArray<int64> BusListenerIds;

	/**
	 * Index into the ring of the slot the NEXT autosave writes. Advances after each successful write so
	 * writes rotate. Seeded from the slot manager's most-recent slot when available (so it survives travel),
	 * else starts at 0.
	 */
	int32 RingCursor = 0;

	/** Time (seconds, FApp::GetCurrentTime) of the last completed autosave request, for throttling. */
	double LastAutosaveTime = -BIG_NUMBER;

	/** Count of autosaves performed this session (debug string). */
	int32 AutosaveCount = 0;

	/** Reason of the most recent autosave (debug string). */
	ESaveX_AutosaveReason LastReason = ESaveX_AutosaveReason::Manual;

	/** Subscribe to the settings-configured trigger channels on the bus. */
	void SubscribeToTriggerChannels();

	/** Periodic interval tick: requests an Interval autosave. Returns true to keep the ticker alive. */
	bool TickInterval(float DeltaTime);

	/** Resolve the slot manager seam (read-only slot metadata). Unset when no backend is registered. */
	TScriptInterface<ISeam_SaveSlotManager> ResolveSlotManager() const;

	/** Resolve the core save subsystem we wrap. May be null in CDO/editor contexts. */
	UDP_SaveGameSubsystem* ResolveSaveSubsystem() const;

	/** Build the ring slot name for a ring index, "<Prefix>_<index>". */
	FString RingSlotNameForIndex(int32 Index) const;

	/** Seed RingCursor from the slot manager's most-recent ring slot so rotation survives level travel. */
	void SeedRingCursorFromSlots();

	/** Build a fresh autosave save object (gather happens in the save object's OnPreSave). */
	UDP_SaveGame* BuildAutosaveObject() const;
};
