// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Slot/SaveX_SlotManagerSubsystem.h"

#include "Settings/SaveX_DeveloperSettings.h"
#include "SaveX_ServiceKeys.h"
#include "DesignPatternsSaveSystemModule.h" // SaveXNativeTags bus channels

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"          // EDP_ServiceLifetime
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Save/DPSaveGame.h"
#include "Save/DPSaveGameSubsystem.h"
#include "Save/DPSaveHeader.h"
#include "Save/DPSaveMigration.h"

#include "Persist/Seam_Persistable.h"

#include "Migration/SaveX_MigrationStep.h"

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"            // TActorIterator
#include "GameFramework/Actor.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Defensive default display name when neither caller nor slot name yields a label. */
	const TCHAR* DefaultDisplayLabel() { return TEXT("Save"); }

	/**
	 * True if the running world has save authority (no save authority on a pure client). The slot
	 * manager is a GameInstance subsystem with no UWorld of its own, so we read the GI's world.
	 * A null world (CDO/early load) is treated as authoritative so a standalone tool/editor flow
	 * is not silently blocked.
	 */
	bool GameInstanceHasSaveAuthority(const UGameInstance* GI)
	{
		if (!GI)
		{
			return true; // No GI context to deny on; defer the real guard to per-participant contracts.
		}
		const UWorld* World = GI->GetWorld();
		return !World || World->GetNetMode() != NM_Client;
	}
}

void USaveX_SlotManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ---- Register migration steps via the CORE subsystem (NOT module startup) ----
	// There is no GameInstance — and therefore no core save subsystem or migration registry — during
	// module load, so this is the correct, GameInstance-scoped place to register version steps. The core
	// registry chains them in ascending order so a very old save is brought fully current on load.
	if (UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem())
	{
		if (UDP_SaveMigration* Migration = Core->GetMigration())
		{
			Migration->RegisterStepClass(USaveX_MigrationStep::StaticClass());
			UE_LOG(LogDPSave, Log,
				TEXT("[SlotManager] Registered %d migration step(s) with the core save subsystem."),
				Migration->NumSteps());
		}
		else
		{
			UE_LOG(LogDPSave, Warning,
				TEXT("[SlotManager] Core save subsystem has no migration registry; steps not registered."));
		}
	}
	else
	{
		// Inert default: with no core subsystem we cannot register migration. The manager still runs (its
		// reads/writes simply report SubsystemUnavailable) — degrade, do not crash.
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] No core save subsystem at Initialize; migration steps not registered."));
	}

	// ---- Publish the ISeam_SaveSlotManager seam in the service locator ----
	if (UDP_ServiceLocatorSubsystem* Locator = GetServiceLocator())
	{
		const FGameplayTag Key = ResolveServiceKey();
		if (Key.IsValid())
		{
			// WeakObserved: the locator must not keep a strong cross-scope reference to a subsystem the
			// engine already owns. If this subsystem is torn down the binding auto-invalidates.
			const bool bRegistered = Locator->RegisterService(
				Key, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
			if (bRegistered)
			{
				RegisteredServiceKey = Key;
				UE_LOG(LogDPSave, Log, TEXT("[SlotManager] Registered ISeam_SaveSlotManager under '%s'."),
					*Key.ToString());
			}
			else
			{
				UE_LOG(LogDPSave, Warning,
					TEXT("[SlotManager] Failed to register the slot-manager seam under '%s'."), *Key.ToString());
			}
		}
		else
		{
			// Documented inert default: an invalid key means consumers cannot resolve the seam, but the
			// manager's direct API still works. Do not register under an empty tag.
			UE_LOG(LogDPSave, Warning,
				TEXT("[SlotManager] No valid service key configured; skipping locator registration."));
		}
	}
}

void USaveX_SlotManagerSubsystem::Deinitialize()
{
	// Unregister exactly the key we registered (a project may have relocated it via settings).
	if (RegisteredServiceKey.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GetServiceLocator())
		{
			Locator->UnregisterService(RegisteredServiceKey);
		}
		RegisteredServiceKey = FGameplayTag();
	}

	// Drop any pending callbacks so a late core async completion cannot invoke into a torn-down manager.
	// The core subsystem binds our UFUNCTIONs weakly by UObject, so this is belt-and-braces bookkeeping.
	PendingSaves.Reset();
	PendingLoads.Reset();

	Super::Deinitialize();
}

// ---- Named-slot operations -----------------------------------------------------------------------

void USaveX_SlotManagerSubsystem::CreateSlot(const FString& SlotName, const FString& DisplayName)
{
	SaveToSlot(SlotName, DisplayName, FSaveX_SlotSaveDone());
}

void USaveX_SlotManagerSubsystem::SaveToSlot(const FString& SlotName, const FString& DisplayName,
	FSaveX_SlotSaveDone OnDone)
{
	// Validate the target name up front so a bad call fails fast (and reports synchronously).
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] SaveToSlot rejected: empty slot name."));
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::InvalidArgument);
		return;
	}

	// A named save must not target a reserved (autosave ring / continue) slot — those are managed by the
	// autosave driver and the continue feature respectively.
	if (IsReservedSlotName(SlotName))
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] SaveToSlot rejected: '%s' is a reserved (ring/continue) slot name."), *SlotName);
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::InvalidArgument);
		return;
	}

	// Enforce the named-slot cap, but ONLY for a brand-new slot. Overwriting an existing named slot is
	// always allowed (it does not grow the count).
	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	const bool bSlotExists = Core ? Core->DoesSlotExist(SlotName) : false;
	if (!bSlotExists)
	{
		const USaveX_DeveloperSettings* Settings = GetSettings();
		const int32 Cap = Settings ? Settings->GetEffectiveMaxNamedSlots() : 16 /*defensive fallback*/;
		if (CountNamedSlots() >= Cap)
		{
			UE_LOG(LogDPSave, Warning,
				TEXT("[SlotManager] SaveToSlot rejected: named-slot cap (%d) reached; cannot create '%s'."),
				Cap, *SlotName);
			OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::SlotLimitReached);
			return;
		}
	}

	InternalSave(SlotName, DisplayName, /*bIsAutosave=*/false, MoveTemp(OnDone));
}

void USaveX_SlotManagerSubsystem::LoadSlot(const FString& SlotName)
{
	LoadSlotWithCallback(SlotName, FSaveX_SlotLoadDone());
}

void USaveX_SlotManagerSubsystem::LoadSlotWithCallback(const FString& SlotName, FSaveX_SlotLoadDone OnDone)
{
	InternalLoad(SlotName, MoveTemp(OnDone));
}

bool USaveX_SlotManagerSubsystem::DeleteSlot(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] DeleteSlot rejected: empty slot name."));
		return false;
	}

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] DeleteSlot('%s'): no core save subsystem."), *SlotName);
		return false;
	}

	const bool bDeleted = Core->DeleteSlot(SlotName);
	if (bDeleted)
	{
		// Notify listeners (e.g. a save/load UI) that a slot vanished. No OnSlotSaved here — this is a delete.
		BroadcastSlotBus(SaveXNativeTags::Bus_SlotDeleted, SlotName);
		UE_LOG(LogDPSave, Log, TEXT("[SlotManager] Deleted slot '%s'."), *SlotName);
	}
	else
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[SlotManager] DeleteSlot('%s'): no file existed."), *SlotName);
	}
	return bDeleted;
}

bool USaveX_SlotManagerSubsystem::RenameSlot(const FString& SlotName, const FString& NewDisplayName)
{
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] RenameSlot rejected: empty slot name."));
		return false;
	}

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] RenameSlot('%s'): no core save subsystem."), *SlotName);
		return false;
	}
	if (!Core->DoesSlotExist(SlotName))
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] RenameSlot('%s'): slot does not exist."), *SlotName);
		return false;
	}

	// The label lives INSIDE the save header (not the file name), so renaming means: load the body,
	// rewrite DisplayName, and re-save under the same slot. We use the synchronous core path so the
	// caller gets a definitive bool result (the body is already on disk; this is a quick rewrite).
	EDP_SaveResult LoadResult = EDP_SaveResult::Success;
	UDP_SaveGame* Loaded = Core->LoadNow(SlotName, LoadResult);
	if (LoadResult != EDP_SaveResult::Success || !Loaded)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] RenameSlot('%s'): load failed (result=%d)."),
			*SlotName, static_cast<int32>(LoadResult));
		return false;
	}

	// Only the player-facing label changes; the gathered body is preserved verbatim so a rename never
	// loses world state. PlaytimeSeconds is carried through unchanged.
	Loaded->DisplayName = NewDisplayName.IsEmpty() ? SlotName : NewDisplayName;

	const EDP_SaveResult SaveResult = Core->SaveNow(SlotName, Loaded);
	if (SaveResult != EDP_SaveResult::Success)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] RenameSlot('%s'): re-save failed (result=%d)."),
			*SlotName, static_cast<int32>(SaveResult));
		return false;
	}

	UE_LOG(LogDPSave, Log, TEXT("[SlotManager] Renamed slot '%s' display label to '%s'."),
		*SlotName, *Loaded->DisplayName);
	return true;
}

void USaveX_SlotManagerSubsystem::ListSlots(TArray<FSeam_SaveSlotInfo>& OutSlots) const
{
	OutSlots.Reset();

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		// Inert default: no backend -> no slots. Callers (UI) render an empty list rather than crashing.
		return;
	}

	const TArray<FString> SlotNames = Core->GetAllSlots();
	OutSlots.Reserve(SlotNames.Num());

	for (const FString& Name : SlotNames)
	{
		FSeam_SaveSlotInfo Info;
		Info.SlotName = Name;
		Info.bExists = true;

		// Read just the header chunk (cheap — no body deserialize) for metadata.
		FDP_SaveHeader Header;
		if (Core->ReadSlotHeader(Name, Header) && Header.IsMagicValid())
		{
			Info.DisplayName = Header.DisplayName.IsEmpty()
				? FText::FromString(Name)
				: FText::FromString(Header.DisplayName);
			Info.Timestamp = Header.TimestampUtc;
			Info.PlaytimeSeconds = Header.PlaytimeSeconds;
		}
		else
		{
			// A slot whose header is unreadable (corrupt/foreign) still appears so the UI can offer a
			// delete, but with a fallback label and a zero timestamp.
			Info.DisplayName = FText::FromString(Name);
			UE_LOG(LogDPSave, Verbose,
				TEXT("[SlotManager] ListSlots: header for '%s' unreadable; using fallback label."), *Name);
		}
		OutSlots.Add(MoveTemp(Info));
	}

	// Newest first — a save/load UI almost always wants most-recent at the top, and it makes "continue"
	// resolution a simple front-of-list read.
	OutSlots.Sort([](const FSeam_SaveSlotInfo& A, const FSeam_SaveSlotInfo& B)
	{
		return A.Timestamp > B.Timestamp;
	});
}

void USaveX_SlotManagerSubsystem::RequestAutosave()
{
	// The slot manager does not own the rotating ring (that is the autosave subsystem's job). To keep a
	// single source of truth for ring rotation + throttle, write the NEXT ring slot computed from headers
	// here and route it through the shared write path flagged as an autosave.
	const FString SlotName = ComputeNextAutosaveSlot();
	if (SlotName.IsEmpty())
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[SlotManager] RequestAutosave: could not compute a ring slot."));
		return;
	}
	InternalSave(SlotName, /*DisplayName=*/TEXT("Autosave"), /*bIsAutosave=*/true, FSaveX_SlotSaveDone());
}

void USaveX_SlotManagerSubsystem::SaveContinue(const FString& DisplayName)
{
	const USaveX_DeveloperSettings* Settings = GetSettings();
	const FString ContinueSlot = (Settings && !Settings->ContinueSlotName.IsEmpty())
		? Settings->ContinueSlotName
		: TEXT("DPContinue") /*defensive fallback*/;

	// The continue slot is always overwritten and is exempt from the named-slot cap, so route it directly
	// through the shared write path (not SaveToSlot, which rejects reserved names).
	InternalSave(ContinueSlot, DisplayName.IsEmpty() ? TEXT("Continue") : DisplayName,
		/*bIsAutosave=*/false, FSaveX_SlotSaveDone());
}

FString USaveX_SlotManagerSubsystem::GetContinueSlot() const
{
	// A "Continue" button should resume the player's most recent progress regardless of how it was saved:
	// the dedicated continue slot, an autosave ring slot, or a named slot — whichever is newest on disk.
	return GetMostRecentSlot_Implementation();
}

// ---- ISeam_SaveSlotManager -----------------------------------------------------------------------

void USaveX_SlotManagerSubsystem::GetAllSlots_Implementation(TArray<FSeam_SaveSlotInfo>& OutSlots) const
{
	ListSlots(OutSlots);
}

FString USaveX_SlotManagerSubsystem::GetMostRecentSlot_Implementation() const
{
	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		return FString();
	}

	FString BestSlot;
	FDateTime BestTime(0);
	bool bFound = false;

	// Walk every slot's header timestamp and keep the newest. Reading headers (not bodies) keeps this
	// cheap enough to call on a menu open.
	for (const FString& Name : Core->GetAllSlots())
	{
		FDP_SaveHeader Header;
		if (Core->ReadSlotHeader(Name, Header) && Header.IsMagicValid())
		{
			if (!bFound || Header.TimestampUtc > BestTime)
			{
				BestTime = Header.TimestampUtc;
				BestSlot = Name;
				bFound = true;
			}
		}
	}
	return BestSlot;
}

bool USaveX_SlotManagerSubsystem::DoesSlotExist_Implementation(const FString& SlotName) const
{
	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	return Core ? Core->DoesSlotExist(SlotName) : false;
}

// ---- Debug ---------------------------------------------------------------------------------------

FString USaveX_SlotManagerSubsystem::GetDPDebugString_Implementation() const
{
	const int32 NamedCount = CountNamedSlots();
	const USaveX_DeveloperSettings* Settings = GetSettings();
	const int32 Cap = Settings ? Settings->GetEffectiveMaxNamedSlots() : 0;
	const FString Recent = GetMostRecentSlot_Implementation();
	return FString::Printf(
		TEXT("SlotManager: named=%d/%d pendingOps=%d recent=%s seamKey=%s"),
		NamedCount, Cap, PendingOps,
		Recent.IsEmpty() ? TEXT("<none>") : *Recent,
		RegisteredServiceKey.IsValid() ? *RegisteredServiceKey.ToString() : TEXT("<unregistered>"));
}

// ---- Internals -----------------------------------------------------------------------------------

const USaveX_DeveloperSettings* USaveX_SlotManagerSubsystem::GetSettings() const
{
	return USaveX_DeveloperSettings::Get();
}

UDP_SaveGameSubsystem* USaveX_SlotManagerSubsystem::GetCoreSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(
		const_cast<USaveX_SlotManagerSubsystem*>(this));
}

UDP_ServiceLocatorSubsystem* USaveX_SlotManagerSubsystem::GetServiceLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(
		const_cast<USaveX_SlotManagerSubsystem*>(this));
}

UDP_SaveGame* USaveX_SlotManagerSubsystem::BuildSaveObjectForWrite(const FString& DisplayName) const
{
	// Choose the project's configured save class (a UDP_SaveGame subclass that gathers/scatters its own
	// world state), falling back to the base UDP_SaveGame which still records header metadata.
	TSubclassOf<UDP_SaveGame> SaveClass = UDP_SaveGame::StaticClass();
	if (const USaveX_DeveloperSettings* Settings = GetSettings())
	{
		if (UClass* Resolved = Settings->SaveGameClass.LoadSynchronous())
		{
			SaveClass = Resolved;
		}
	}

	UDP_SaveGame* SaveObject = NewObject<UDP_SaveGame>(GetTransientPackage(), SaveClass);
	if (!SaveObject)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] Failed to construct a save object of class '%s'."),
			*GetNameSafe(SaveClass));
		return nullptr;
	}

	// Stamp the player-facing header metadata. A blank label falls back to a stable default so the slot
	// UI never shows an empty row (the core copies these into the header at serialize time).
	SaveObject->DisplayName = DisplayName.IsEmpty() ? DefaultDisplayLabel() : DisplayName;

	// PlaytimeSeconds: the project's save subclass is the authority on accumulated playtime (it tracks the
	// session clock and writes it in its own OnPreSave). We leave the gathered value in place and only set
	// a floor of 0 defensively. (No magic number — 0 simply means "unknown / not yet tracked".)
	SaveObject->PlaytimeSeconds = FMath::Max(0.f, SaveObject->PlaytimeSeconds);

	// GATHER: drive the cross-world ISeam_Persistable participants. Authority-guarded (a client must not
	// read server-authoritative state it does not own) and weak-pruned (TActorIterator already skips
	// invalid/CDO actors; we re-check IsValid before every interface call). Each participant's captured
	// record is handed to the save object IF the save object itself implements ISeam_Persistable and can
	// aggregate records (the engine pattern: a project save object owns the aggregate store). When the
	// save object does not implement the seam, participants still run their CaptureState side-effect-free
	// gather, and the save object's own OnPreSave (run by the core at serialize time) performs the final
	// body gather — so no state is lost in either configuration.
	GatherParticipantsInto(SaveObject);

	return SaveObject;
}

void USaveX_SlotManagerSubsystem::GatherParticipantsInto(UDP_SaveGame* SaveObject) const
{
	if (!SaveObject)
	{
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		// No live world to gather from (e.g. a tool flow). The save object's own OnPreSave still runs in
		// the core path; nothing to do here.
		return;
	}

	// AUTHORITY GUARD (top-level): on a pure client we do not gather authoritative world state. The
	// per-participant RestoreState contract is also authority-guarded, but gather is guarded here too so
	// a client save is a clean header-only write rather than a partial read of replicated proxies.
	if (!GameInstanceHasSaveAuthority(GI))
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[SlotManager] Gather skipped on non-authority; writing header-only save."));
		return;
	}

	// Does the save object aggregate participant records? (A project subclass that implements the seam
	// owns the aggregate store and routes records via its own RestoreState.) If not, we still iterate so
	// each participant's CaptureState runs, but we have nowhere to deposit the record.
	const bool bSaveAggregates =
		SaveObject->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass());

	int32 GatheredCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		// An actor may itself be a participant, and/or own component participants. Collect both.
		TArray<UObject*, TInlineAllocator<4>> Participants;
		if (Actor->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
		{
			Participants.Add(Actor);
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
			{
				Participants.Add(Comp);
			}
		}

		for (UObject* Participant : Participants)
		{
			if (!IsValid(Participant))
			{
				continue;
			}

			// Capture this participant's durable state. CaptureState is const/read-only by contract.
			FInstancedStruct Record;
			ISeam_Persistable::Execute_CaptureState(Participant, Record);
			if (!Record.IsValid())
			{
				// A participant with nothing to persist this frame. Skip — its kind tag adds no value.
				continue;
			}

			if (bSaveAggregates)
			{
				// Deposit the record into the save object's aggregate store. RestoreState on a SAVE object
				// is the engine pattern for "absorb a record into the aggregate" — the save subclass's
				// RestoreState merges incoming records (see the sibling project save objects). The save
				// object is a transient, locally-owned gather buffer, so this is an authority-safe write.
				ISeam_Persistable::Execute_RestoreState(SaveObject, Record);
			}

			++GatheredCount;
		}
	}

	UE_CLOG(GatheredCount > 0, LogDPSave, Verbose,
		TEXT("[SlotManager] Gathered %d persistable record(s) into '%s' (aggregated=%s)."),
		GatheredCount, *GetNameSafe(SaveObject), bSaveAggregates ? TEXT("yes") : TEXT("no"));
}

void USaveX_SlotManagerSubsystem::InternalSave(const FString& SlotName, const FString& DisplayName,
	bool bIsAutosave, FSaveX_SlotSaveDone OnDone)
{
	if (SlotName.IsEmpty())
	{
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::InvalidArgument);
		return;
	}

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] InternalSave('%s'): no core save subsystem."), *SlotName);
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::SubsystemUnavailable);
		return;
	}

	UDP_SaveGame* SaveObject = BuildSaveObjectForWrite(DisplayName);
	if (!SaveObject)
	{
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::CoreFailure);
		return;
	}

	// Record the pending write keyed by slot so the dynamic core callback can recover the native OnDone
	// and the autosave flag. A second write to the same slot before the first completes overwrites the
	// pending entry — last-writer-wins matches the on-disk overwrite semantics. When that happens we must
	// (a) NOT double-count PendingOps (one core callback is still in flight for this slot), and (b) notify
	// the superseded caller so it is never left waiting on a callback that will never fire.
	FPendingSave Pending;
	Pending.bIsAutosave = bIsAutosave;
	Pending.OnDone = MoveTemp(OnDone);
	if (FPendingSave* Superseded = PendingSaves.Find(SlotName))
	{
		Superseded->OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::Superseded);
		// Overwrite in place; PendingOps already counts the in-flight op for this slot.
		*Superseded = MoveTemp(Pending);
	}
	else
	{
		PendingSaves.Add(SlotName, MoveTemp(Pending));
		++PendingOps;
	}

	// Bind the core's per-call dynamic callback to our UFUNCTION (UObject-weak by construction).
	FDP_SaveCallbackDynamic OnComplete;
	OnComplete.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(USaveX_SlotManagerSubsystem, HandleCoreSaveComplete));

	// Hand off to the core — it owns ALL byte/header/async-IO. We reinvent no serialization.
	Core->SaveAsync(SlotName, SaveObject, OnComplete);

	UE_LOG(LogDPSave, Log, TEXT("[SlotManager] Writing slot '%s' (autosave=%s, label='%s')."),
		*SlotName, bIsAutosave ? TEXT("yes") : TEXT("no"), *SaveObject->DisplayName);
}

void USaveX_SlotManagerSubsystem::InternalLoad(const FString& SlotName, FSaveX_SlotLoadDone OnDone)
{
	if (SlotName.IsEmpty())
	{
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::InvalidArgument, nullptr);
		return;
	}

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[SlotManager] InternalLoad('%s'): no core save subsystem."), *SlotName);
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::SubsystemUnavailable, nullptr);
		return;
	}
	if (!Core->DoesSlotExist(SlotName))
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] InternalLoad('%s'): slot not found."), *SlotName);
		OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::SlotNotFound, nullptr);
		return;
	}

	// As with InternalSave: a second load of the same slot before the first completes overwrites the
	// pending entry. Notify the superseded caller and do not double-count PendingOps.
	FPendingLoad Pending;
	Pending.OnDone = MoveTemp(OnDone);
	if (FPendingLoad* Superseded = PendingLoads.Find(SlotName))
	{
		Superseded->OnDone.ExecuteIfBound(SlotName, ESaveX_SlotResult::Superseded, nullptr);
		*Superseded = MoveTemp(Pending);
	}
	else
	{
		PendingLoads.Add(SlotName, MoveTemp(Pending));
		++PendingOps;
	}

	FDP_LoadCallbackDynamic OnComplete;
	OnComplete.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(USaveX_SlotManagerSubsystem, HandleCoreLoadComplete));

	Core->LoadAsync(SlotName, OnComplete);

	UE_LOG(LogDPSave, Log, TEXT("[SlotManager] Loading slot '%s'."), *SlotName);
}

void USaveX_SlotManagerSubsystem::ScatterToParticipants(UDP_SaveGame* Loaded) const
{
	if (!Loaded)
	{
		return;
	}

	// AUTHORITY GUARD: scattering mutates authoritative world state. The ISeam_Persistable::RestoreState
	// contract requires each participant to authority-guard, but we additionally short-circuit on a pure
	// client so we do not even iterate the world.
	if (!GameInstanceHasSaveAuthority(GetGameInstance()))
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[SlotManager] Scatter skipped on non-authority; participants restore on the host only."));
		return;
	}

	// If the save object aggregates records, ask it to capture the aggregate, then route each record to
	// the world participant whose persistence-kind matches. When the save object is a project subclass
	// that scatters in its own OnPostLoad (already run by the core load path), this is a harmless second
	// pass for any participant it could not reach directly.
	const bool bSaveAggregates =
		Loaded->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass());
	if (!bSaveAggregates)
	{
		// The project save subclass scattered in OnPostLoad already; nothing generic to route here.
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// Pull the aggregate record the save object holds and broadcast it to every matching participant by
	// kind. The save object's own RestoreState defines what a single aggregate record contains.
	FInstancedStruct Aggregate;
	ISeam_Persistable::Execute_CaptureState(Loaded, Aggregate);
	if (!Aggregate.IsValid())
	{
		return;
	}

	const FGameplayTag SaveKind = ISeam_Persistable::Execute_GetPersistenceKind(Loaded);

	int32 ScatterCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<UObject*, TInlineAllocator<4>> Participants;
		if (Actor->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
		{
			Participants.Add(Actor);
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
			{
				Participants.Add(Comp);
			}
		}

		for (UObject* Participant : Participants)
		{
			if (!IsValid(Participant) || Participant == Loaded)
			{
				continue;
			}
			// Route by kind so a record only reaches the participant type it belongs to. A participant
			// with no kind (invalid tag) is treated as a wildcard receiver for the aggregate.
			const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Participant);
			if (!SaveKind.IsValid() || !Kind.IsValid() || Kind.MatchesTag(SaveKind) || SaveKind.MatchesTag(Kind))
			{
				ISeam_Persistable::Execute_RestoreState(Participant, Aggregate);
				++ScatterCount;
			}
		}
	}

	UE_CLOG(ScatterCount > 0, LogDPSave, Verbose,
		TEXT("[SlotManager] Scattered loaded state to %d participant(s)."), ScatterCount);
}

bool USaveX_SlotManagerSubsystem::IsReservedSlotName(const FString& SlotName) const
{
	const USaveX_DeveloperSettings* Settings = GetSettings();

	// Continue slot is reserved.
	const FString ContinueSlot = (Settings && !Settings->ContinueSlotName.IsEmpty())
		? Settings->ContinueSlotName : TEXT("DPContinue");
	if (SlotName.Equals(ContinueSlot, ESearchCase::IgnoreCase))
	{
		return true;
	}

	// Any autosave ring slot ("<Prefix>_<index>") is reserved.
	const FString Prefix = (Settings && !Settings->AutosaveSlotPrefix.IsEmpty())
		? Settings->AutosaveSlotPrefix : TEXT("DPAutosave");
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;
	for (int32 Index = 0; Index < RingSize; ++Index)
	{
		const FString RingName = FString::Printf(TEXT("%s_%d"), *Prefix, Index);
		if (SlotName.Equals(RingName, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

int32 USaveX_SlotManagerSubsystem::CountNamedSlots() const
{
	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		return 0;
	}
	int32 Count = 0;
	for (const FString& Name : Core->GetAllSlots())
	{
		if (!IsReservedSlotName(Name))
		{
			++Count;
		}
	}
	return Count;
}

FString USaveX_SlotManagerSubsystem::ComputeNextAutosaveSlot() const
{
	const USaveX_DeveloperSettings* Settings = GetSettings();
	const FString Prefix = (Settings && !Settings->AutosaveSlotPrefix.IsEmpty())
		? Settings->AutosaveSlotPrefix : TEXT("DPAutosave");
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;

	UDP_SaveGameSubsystem* Core = GetCoreSaveSubsystem();
	if (!Core)
	{
		// No backend to read timestamps from: default to the first ring slot deterministically.
		return FString::Printf(TEXT("%s_0"), *Prefix);
	}

	// Pick the OLDEST ring slot (oldest header timestamp; an absent slot counts as oldest of all so empty
	// ring members fill first). This makes the ring self-healing even after level travel, without an
	// in-memory cursor.
	FString OldestSlot;
	FDateTime OldestTime = FDateTime::MaxValue();
	for (int32 Index = 0; Index < RingSize; ++Index)
	{
		const FString RingName = FString::Printf(TEXT("%s_%d"), *Prefix, Index);
		if (!Core->DoesSlotExist(RingName))
		{
			// An empty ring member is the best candidate — fill it before overwriting anything.
			return RingName;
		}
		FDP_SaveHeader Header;
		const FDateTime When = (Core->ReadSlotHeader(RingName, Header) && Header.IsMagicValid())
			? Header.TimestampUtc : FDateTime(0);
		if (OldestSlot.IsEmpty() || When < OldestTime)
		{
			OldestTime = When;
			OldestSlot = RingName;
		}
	}

	return OldestSlot.IsEmpty() ? FString::Printf(TEXT("%s_0"), *Prefix) : OldestSlot;
}

ESaveX_SlotResult USaveX_SlotManagerSubsystem::FromCoreResult(EDP_SaveResult CoreResult)
{
	switch (CoreResult)
	{
	case EDP_SaveResult::Success:        return ESaveX_SlotResult::Success;
	case EDP_SaveResult::InvalidArgument: return ESaveX_SlotResult::InvalidArgument;
	case EDP_SaveResult::SlotNotFound:   return ESaveX_SlotResult::SlotNotFound;
	default:                             return ESaveX_SlotResult::CoreFailure;
	}
}

void USaveX_SlotManagerSubsystem::BroadcastSlotBus(FGameplayTag Channel, const FString& SlotName) const
{
	if (!Channel.IsValid())
	{
		return;
	}
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(
			const_cast<USaveX_SlotManagerSubsystem*>(this));
	if (!Bus)
	{
		// Inert default: no bus -> no broadcast. Listeners (if any) simply do not get this event.
		return;
	}

	FSaveX_SlotBusPayload Payload;
	Payload.SlotName = SlotName;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload),
		const_cast<USaveX_SlotManagerSubsystem*>(this));
}

FGameplayTag USaveX_SlotManagerSubsystem::ResolveServiceKey() const
{
	if (const USaveX_DeveloperSettings* Settings = GetSettings())
	{
		if (Settings->SlotManagerServiceTag.IsValid())
		{
			return Settings->SlotManagerServiceTag;
		}
	}
	// Fall back to the conventional SaveSystem key so an unedited project still publishes the seam.
	return SaveX_ServiceKeys::SlotManager();
}

void USaveX_SlotManagerSubsystem::HandleCoreSaveComplete(FString Slot, EDP_SaveResult Result)
{
	PendingOps = FMath::Max(0, PendingOps - 1);

	const bool bSuccess = (Result == EDP_SaveResult::Success);

	// Recover (and remove) the pending entry for this slot.
	FPendingSave Pending;
	const bool bHadPending = PendingSaves.RemoveAndCopyValue(Slot, Pending);

	if (bSuccess)
	{
		// Broadcast on the appropriate channel: the rotating ring uses the Autosaved channel; everything
		// else (named, continue) uses SlotSaved.
		const bool bIsAutosave = bHadPending && Pending.bIsAutosave;
		BroadcastSlotBus(bIsAutosave ? SaveXNativeTags::Bus_Autosaved : SaveXNativeTags::Bus_SlotSaved, Slot);
	}
	else
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] Save of '%s' failed (result=%d)."),
			*Slot, static_cast<int32>(Result));
	}

	// Fire the BP-assignable multicast and the per-call native callback (in that order so a BP UI updates
	// before any native completion logic runs).
	OnSlotSaved.Broadcast(Slot, bSuccess);
	if (bHadPending)
	{
		Pending.OnDone.ExecuteIfBound(Slot, FromCoreResult(Result));
	}
}

void USaveX_SlotManagerSubsystem::HandleCoreLoadComplete(FString Slot, EDP_SaveResult Result, UDP_SaveGame* SaveObject)
{
	PendingOps = FMath::Max(0, PendingOps - 1);

	const bool bSuccess = (Result == EDP_SaveResult::Success) && (SaveObject != nullptr);

	FPendingLoad Pending;
	const bool bHadPending = PendingLoads.RemoveAndCopyValue(Slot, Pending);

	if (bSuccess)
	{
		// SCATTER: push restored state back into the live world via participant RestoreState (authority-
		// guarded). The core load path has already run the save object's own OnPostLoad; this routes any
		// aggregate records to matching world participants.
		ScatterToParticipants(SaveObject);
		BroadcastSlotBus(SaveXNativeTags::Bus_SlotLoaded, Slot);
	}
	else
	{
		UE_LOG(LogDPSave, Warning, TEXT("[SlotManager] Load of '%s' failed (result=%d)."),
			*Slot, static_cast<int32>(Result));
	}

	OnSlotLoaded.Broadcast(Slot, bSuccess, SaveObject);
	if (bHadPending)
	{
		Pending.OnDone.ExecuteIfBound(Slot, FromCoreResult(Result), bSuccess ? SaveObject : nullptr);
	}
}
