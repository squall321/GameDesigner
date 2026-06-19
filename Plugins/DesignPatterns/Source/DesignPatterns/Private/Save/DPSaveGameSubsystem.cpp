// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/DPSaveGameSubsystem.h"
#include "Save/DPSaveGame.h"
#include "Save/DPSaveHeader.h"
#include "Save/DPSaveVersion.h"
#include "Save/DPSaveMigration.h"
#include "Core/DPLog.h"
#include "Core/DPDeveloperSettings.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Stats/Stats.h"
#include "UObject/UObjectGlobals.h"

DECLARE_CYCLE_STAT(TEXT("Save Serialize To Bytes"), STAT_DPSaveSerialize, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Save Deserialize From Bytes"), STAT_DPSaveDeserialize, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Save Pending Async Ops"), STAT_DPSavePending, STATGROUP_DesignPatterns);

namespace
{
	/** A proxy archive that string-encodes object/name refs so saves survive package changes,
	 *  and binds the DP custom version so loaders see the on-disk format version. */
	class FDP_SaveArchive : public FObjectAndNameAsStringProxyArchive
	{
	public:
		FDP_SaveArchive(FArchive& InInnerArchive, bool bInLoadIfFindFails)
			: FObjectAndNameAsStringProxyArchive(InInnerArchive, bInLoadIfFindFails)
		{
			ArIsSaveGame = true;          // only serialize SaveGame-tagged UPROPERTYs
			ArNoDelta = true;             // full serialize, no delta against CDO
			SetIsPersistent(true);
			SetCustomVersion(FDP_SaveVersion::GUID, FDP_SaveVersion::LatestVersion, TEXT("DPSaveVersion"));
		}
	};
}

void UDP_SaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Ensure the custom version is registered before any (de)serialization happens.
	FDP_SaveVersion::Register();

	if (const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLoggingByDefault;
	}

	Migration = NewObject<UDP_SaveMigration>(this, TEXT("DP_SaveMigration"));

	UE_LOG(LogDPSave, Verbose, TEXT("SaveGameSubsystem initialized (latest format version=%d)."),
		(int32)FDP_SaveVersion::LatestVersion);
}

void UDP_SaveGameSubsystem::Deinitialize()
{
	check(IsInGameThread());

	// Threading contract (why no blocking wait here):
	//  - PendingOps is incremented/decremented ONLY on the game thread (SaveAsync/LoadAsync run on
	//    the game thread; the decrement happens in an ENamedThreads::GameThread continuation), so it
	//    needs no atomic and there is no read/write race.
	//  - The background ThreadPool task only touches a moved TArray<uint8> and never this UObject.
	//  - The game-thread continuation captures a TWeakObjectPtr<UDP_SaveGameSubsystem>, so if we are
	//    gone it no-ops on all subsystem state (PendingOps/Migration/delegates).
	// A spin-wait on the game thread would hitch every level transition for no correctness gain, so
	// we intentionally do not block. We only surface a diagnostic if IO is still outstanding.
	if (PendingOps > 0)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("SaveGameSubsystem deinitialized with %d async op(s) still in flight. ")
			TEXT("Their game-thread callbacks will no-op on subsystem state (weak-ref guarded); ")
			TEXT("in-progress disk writes still finish on the thread pool."), PendingOps);
	}

	// Migration is only ever read on the game thread (deserialize path), so clearing it here is safe.
	Migration = nullptr;
	Super::Deinitialize();
}

FString UDP_SaveGameSubsystem::SlotToFilePath(const FString& Slot)
{
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	return FPaths::Combine(SaveDir, Slot + TEXT(".") + SaveExtension());
}

// ---------------------------------------------------------------------------------------
// Serialization (game thread)
// ---------------------------------------------------------------------------------------

EDP_SaveResult UDP_SaveGameSubsystem::SerializeToBytes(UDP_SaveGame* SaveObject, TArray<uint8>& OutBytes) const
{
	SCOPE_CYCLE_COUNTER(STAT_DPSaveSerialize);
	check(IsInGameThread());

	if (!SaveObject)
	{
		return EDP_SaveResult::InvalidArgument;
	}

	// Gather hook first.
	SaveObject->OnPreSave();

	// --- Serialize the body to its own buffer ---
	TArray<uint8> BodyBytes;
	{
		FMemoryWriter BodyWriter(BodyBytes, /*bIsPersistent*/ true);
		FDP_SaveArchive Ar(BodyWriter, /*bLoadIfFindFails*/ false);
		SaveObject->Serialize(Ar);
		if (Ar.IsError())
		{
			return EDP_SaveResult::SerializationFailed;
		}
	}

	// --- Build the header ---
	FDP_SaveHeader Header;
	Header.Magic = FDP_SaveHeader::DefaultMagic();
	Header.SaveVersion = FDP_SaveVersion::LatestVersion;
	Header.SaveGameClassPath = SaveObject->GetClass()->GetPathName();
	Header.TimestampUtc = FDateTime::UtcNow();
	Header.DisplayName = SaveObject->DisplayName;
	Header.PlaytimeSeconds = SaveObject->PlaytimeSeconds;

	TArray<uint8> HeaderBytes;
	{
		FMemoryWriter HeaderWriter(HeaderBytes, /*bIsPersistent*/ true);
		Header.Serialize(HeaderWriter);
		if (HeaderWriter.IsError())
		{
			return EDP_SaveResult::SerializationFailed;
		}
	}

	// --- Assemble the length-prefixed blob: [hdrLen][hdr][bodyLen][body] ---
	OutBytes.Reset();
	FMemoryWriter BlobWriter(OutBytes, /*bIsPersistent*/ true);
	int64 HeaderLen = HeaderBytes.Num();
	int64 BodyLen = BodyBytes.Num();
	BlobWriter << HeaderLen;
	BlobWriter.Serialize(HeaderBytes.GetData(), HeaderBytes.Num());
	BlobWriter << BodyLen;
	BlobWriter.Serialize(BodyBytes.GetData(), BodyBytes.Num());

	return BlobWriter.IsError() ? EDP_SaveResult::SerializationFailed : EDP_SaveResult::Success;
}

EDP_SaveResult UDP_SaveGameSubsystem::ParseHeaderChunk(const TArray<uint8>& Bytes, FDP_SaveHeader& OutHeader, int64& OutBodyOffset)
{
	FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);

	if (Bytes.Num() < (int32)sizeof(int64))
	{
		return EDP_SaveResult::CorruptData;
	}

	int64 HeaderLen = 0;
	Reader << HeaderLen;
	if (HeaderLen <= 0 || Reader.Tell() + HeaderLen > Bytes.Num())
	{
		return EDP_SaveResult::CorruptData;
	}

	// Read the header chunk into its own sub-buffer and parse it.
	TArray<uint8> HeaderBytes;
	HeaderBytes.SetNumUninitialized(HeaderLen);
	Reader.Serialize(HeaderBytes.GetData(), HeaderLen);

	{
		FMemoryReader HeaderReader(HeaderBytes, /*bIsPersistent*/ true);
		OutHeader.Serialize(HeaderReader);
		if (HeaderReader.IsError())
		{
			return EDP_SaveResult::CorruptData;
		}
	}

	if (!OutHeader.IsMagicValid())
	{
		return EDP_SaveResult::CorruptData;
	}

	OutBodyOffset = Reader.Tell();
	return EDP_SaveResult::Success;
}

EDP_SaveResult UDP_SaveGameSubsystem::DeserializeFromBytes(const TArray<uint8>& Bytes, bool bDeserializeBody,
	FDP_SaveHeader& OutHeader, UDP_SaveGame*& OutSave) const
{
	SCOPE_CYCLE_COUNTER(STAT_DPSaveDeserialize);
	check(IsInGameThread());
	OutSave = nullptr;

	int64 BodyOffset = 0;
	const EDP_SaveResult HeaderResult = ParseHeaderChunk(Bytes, OutHeader, BodyOffset);
	if (HeaderResult != EDP_SaveResult::Success)
	{
		return HeaderResult;
	}

	if (!bDeserializeBody)
	{
		return EDP_SaveResult::Success;
	}

	// Resolve the concrete save class from the header.
	UClass* SaveClass = FindObject<UClass>(nullptr, *OutHeader.SaveGameClassPath);
	if (!SaveClass)
	{
		SaveClass = LoadObject<UClass>(nullptr, *OutHeader.SaveGameClassPath);
	}
	if (!SaveClass || !SaveClass->IsChildOf(UDP_SaveGame::StaticClass()))
	{
		UE_LOG(LogDPSave, Error, TEXT("Could not resolve save class '%s'."), *OutHeader.SaveGameClassPath);
		return EDP_SaveResult::ClassResolutionFailed;
	}

	// Read the body chunk length + bytes.
	FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
	Reader.Seek(BodyOffset);
	if (Reader.Tell() + (int64)sizeof(int64) > Bytes.Num())
	{
		return EDP_SaveResult::CorruptData;
	}
	int64 BodyLen = 0;
	Reader << BodyLen;
	if (BodyLen < 0 || Reader.Tell() + BodyLen > Bytes.Num())
	{
		return EDP_SaveResult::CorruptData;
	}
	TArray<uint8> BodyBytes;
	BodyBytes.SetNumUninitialized(BodyLen);
	Reader.Serialize(BodyBytes.GetData(), BodyLen);

	// Construct and deserialize the save object (transient outer keeps it out of any package).
	UDP_SaveGame* NewSave = NewObject<UDP_SaveGame>(GetTransientPackage(), SaveClass);
	if (!NewSave)
	{
		return EDP_SaveResult::ClassResolutionFailed;
	}
	NewSave->LoadedFromVersion = OutHeader.SaveVersion;

	{
		FMemoryReader BodyReader(BodyBytes, /*bIsPersistent*/ true);
		FDP_SaveArchive Ar(BodyReader, /*bLoadIfFindFails*/ true);
		NewSave->Serialize(Ar);
		if (Ar.IsError())
		{
			return EDP_SaveResult::SerializationFailed;
		}
	}

	// Restore header-mirrored metadata onto the object.
	NewSave->DisplayName = OutHeader.DisplayName;
	NewSave->PlaytimeSeconds = OutHeader.PlaytimeSeconds;

	// Migration: registry chain first, then the object's own hook.
	const int32 Target = FDP_SaveVersion::LatestVersion;
	if (OutHeader.SaveVersion < Target)
	{
		if (Migration && !Migration->Migrate(NewSave, OutHeader.SaveVersion, Target))
		{
			// Registry couldn't finish; fall through to the object hook which may complete it.
			UE_LOG(LogDPSave, Verbose, TEXT("Registry migration incomplete; relying on save object's Migrate()."));
		}
		if (!NewSave->Migrate(OutHeader.SaveVersion, Target))
		{
			UE_LOG(LogDPSave, Error, TEXT("Save object migration hook failed (%d -> %d)."), OutHeader.SaveVersion, Target);
			return EDP_SaveResult::MigrationFailed;
		}
		NewSave->LoadedFromVersion = Target;
	}

	NewSave->OnPostLoad();

	OutSave = NewSave;
	return EDP_SaveResult::Success;
}

// ---------------------------------------------------------------------------------------
// Synchronous API
// ---------------------------------------------------------------------------------------

EDP_SaveResult UDP_SaveGameSubsystem::SaveNow(const FString& Slot, UDP_SaveGame* SaveObject)
{
	if (Slot.IsEmpty() || !SaveObject)
	{
		return EDP_SaveResult::InvalidArgument;
	}

	TArray<uint8> Bytes;
	const EDP_SaveResult SerResult = SerializeToBytes(SaveObject, Bytes);
	if (SerResult != EDP_SaveResult::Success)
	{
		OnSaveCompleted.Broadcast(Slot, SerResult);
		return SerResult;
	}

	const FString FilePath = SlotToFilePath(Slot);
	const bool bWritten = FFileHelper::SaveArrayToFile(Bytes, *FilePath);
	const EDP_SaveResult Result = bWritten ? EDP_SaveResult::Success : EDP_SaveResult::IOFailed;

	UE_LOG(LogDPSave, Log, TEXT("SaveNow('%s') -> %s (%d bytes)."),
		*Slot, bWritten ? TEXT("OK") : TEXT("IO FAILED"), Bytes.Num());

	OnSaveCompleted.Broadcast(Slot, Result);
	return Result;
}

UDP_SaveGame* UDP_SaveGameSubsystem::LoadNow(const FString& Slot, EDP_SaveResult& OutResult)
{
	if (Slot.IsEmpty())
	{
		OutResult = EDP_SaveResult::InvalidArgument;
		return nullptr;
	}
	const FString FilePath = SlotToFilePath(Slot);
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		OutResult = EDP_SaveResult::SlotNotFound;
		OnLoadCompleted.Broadcast(Slot, OutResult, nullptr);
		return nullptr;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		OutResult = EDP_SaveResult::IOFailed;
		OnLoadCompleted.Broadcast(Slot, OutResult, nullptr);
		return nullptr;
	}

	FDP_SaveHeader Header;
	UDP_SaveGame* Loaded = nullptr;
	OutResult = DeserializeFromBytes(Bytes, /*bDeserializeBody*/ true, Header, Loaded);

	UE_LOG(LogDPSave, Log, TEXT("LoadNow('%s') -> %s."), *Slot, OutResult == EDP_SaveResult::Success ? TEXT("OK") : TEXT("FAILED"));

	OnLoadCompleted.Broadcast(Slot, OutResult, Loaded);
	return Loaded;
}

// ---------------------------------------------------------------------------------------
// Async API  (serialize on game thread; only bytes cross to the IO task)
// ---------------------------------------------------------------------------------------

void UDP_SaveGameSubsystem::SaveAsync(const FString& Slot, UDP_SaveGame* SaveObject, const FDP_SaveCallbackDynamic& OnComplete)
{
	if (Slot.IsEmpty() || !SaveObject)
	{
		OnComplete.ExecuteIfBound(Slot, EDP_SaveResult::InvalidArgument);
		OnSaveCompleted.Broadcast(Slot, EDP_SaveResult::InvalidArgument);
		return;
	}

	// 1) GAME THREAD: gather + serialize into a byte buffer. No UObject leaves this thread.
	TArray<uint8> Bytes;
	const EDP_SaveResult SerResult = SerializeToBytes(SaveObject, Bytes);
	if (SerResult != EDP_SaveResult::Success)
	{
		OnComplete.ExecuteIfBound(Slot, SerResult);
		OnSaveCompleted.Broadcast(Slot, SerResult);
		return;
	}

	const FString FilePath = SlotToFilePath(Slot);
	TWeakObjectPtr<UDP_SaveGameSubsystem> WeakThis(this);

	++PendingOps;
	SET_DWORD_STAT(STAT_DPSavePending, PendingOps);

	// 2) BACKGROUND: write the bytes (a plain TArray<uint8> copy — thread-safe to move).
	Async(EAsyncExecution::ThreadPool,
		[Bytes = MoveTemp(Bytes), FilePath, Slot, WeakThis, OnComplete]() mutable
		{
			const bool bWritten = FFileHelper::SaveArrayToFile(Bytes, *FilePath);
			const EDP_SaveResult Result = bWritten ? EDP_SaveResult::Success : EDP_SaveResult::IOFailed;

			// 3) GAME THREAD: fire callbacks/delegates and bookkeeping.
			AsyncTask(ENamedThreads::GameThread, [Result, Slot, WeakThis, OnComplete]()
			{
				if (UDP_SaveGameSubsystem* Self = WeakThis.Get())
				{
					--Self->PendingOps;
					SET_DWORD_STAT(STAT_DPSavePending, Self->PendingOps);
					Self->OnSaveCompleted.Broadcast(Slot, Result);
				}
				OnComplete.ExecuteIfBound(Slot, Result);
				UE_LOG(LogDPSave, Log, TEXT("SaveAsync('%s') -> %s."), *Slot,
					Result == EDP_SaveResult::Success ? TEXT("OK") : TEXT("IO FAILED"));
			});
		});
}

void UDP_SaveGameSubsystem::LoadAsync(const FString& Slot, const FDP_LoadCallbackDynamic& OnComplete)
{
	if (Slot.IsEmpty())
	{
		OnComplete.ExecuteIfBound(Slot, EDP_SaveResult::InvalidArgument, nullptr);
		OnLoadCompleted.Broadcast(Slot, EDP_SaveResult::InvalidArgument, nullptr);
		return;
	}

	const FString FilePath = SlotToFilePath(Slot);
	TWeakObjectPtr<UDP_SaveGameSubsystem> WeakThis(this);

	++PendingOps;
	SET_DWORD_STAT(STAT_DPSavePending, PendingOps);

	// 1) BACKGROUND: read raw bytes only — no UObject work off the game thread.
	Async(EAsyncExecution::ThreadPool, [FilePath, Slot, WeakThis, OnComplete]()
	{
		TArray<uint8> Bytes;
		EDP_SaveResult IOResult = EDP_SaveResult::Success;
		if (!IFileManager::Get().FileExists(*FilePath))
		{
			IOResult = EDP_SaveResult::SlotNotFound;
		}
		else if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			IOResult = EDP_SaveResult::IOFailed;
		}

		// 2) GAME THREAD: parse/deserialize/migrate (all UObject work) and fire callbacks.
		AsyncTask(ENamedThreads::GameThread,
			[Bytes = MoveTemp(Bytes), IOResult, Slot, WeakThis, OnComplete]() mutable
			{
				UDP_SaveGame* Loaded = nullptr;
				EDP_SaveResult Result = IOResult;

				UDP_SaveGameSubsystem* Self = WeakThis.Get();
				if (Self)
				{
					--Self->PendingOps;
					SET_DWORD_STAT(STAT_DPSavePending, Self->PendingOps);
				}

				if (Result == EDP_SaveResult::Success && Self)
				{
					FDP_SaveHeader Header;
					Result = Self->DeserializeFromBytes(Bytes, /*bDeserializeBody*/ true, Header, Loaded);
				}
				else if (Result == EDP_SaveResult::Success && !Self)
				{
					// Subsystem gone before the game-thread continuation ran.
					Result = EDP_SaveResult::InvalidArgument;
				}

				if (Self)
				{
					Self->OnLoadCompleted.Broadcast(Slot, Result, Loaded);
				}
				OnComplete.ExecuteIfBound(Slot, Result, Loaded);
				UE_LOG(LogDPSave, Log, TEXT("LoadAsync('%s') -> %s."), *Slot,
					Result == EDP_SaveResult::Success ? TEXT("OK") : TEXT("FAILED"));
			});
	});
}

// ---------------------------------------------------------------------------------------
// Slot management
// ---------------------------------------------------------------------------------------

bool UDP_SaveGameSubsystem::DeleteSlot(const FString& Slot)
{
	if (Slot.IsEmpty())
	{
		return false;
	}
	const FString FilePath = SlotToFilePath(Slot);
	if (!IFileManager::Get().FileExists(*FilePath))
	{
		return false;
	}
	const bool bDeleted = IFileManager::Get().Delete(*FilePath, /*bRequireExists*/ false, /*bEvenIfReadOnly*/ true);
	UE_LOG(LogDPSave, Log, TEXT("DeleteSlot('%s') -> %s."), *Slot, bDeleted ? TEXT("OK") : TEXT("FAILED"));
	return bDeleted;
}

bool UDP_SaveGameSubsystem::DoesSlotExist(const FString& Slot) const
{
	if (Slot.IsEmpty())
	{
		return false;
	}
	return IFileManager::Get().FileExists(*SlotToFilePath(Slot));
}

TArray<FString> UDP_SaveGameSubsystem::GetAllSlots() const
{
	TArray<FString> Slots;
	const FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	const FString Wildcard = FPaths::Combine(SaveDir, FString(TEXT("*.")) + SaveExtension());

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *Wildcard, /*Files*/ true, /*Directories*/ false);
	Slots.Reserve(Files.Num());
	for (const FString& File : Files)
	{
		Slots.Add(FPaths::GetBaseFilename(File));
	}
	return Slots;
}

bool UDP_SaveGameSubsystem::ReadSlotHeader(const FString& Slot, FDP_SaveHeader& OutHeader) const
{
	if (Slot.IsEmpty())
	{
		return false;
	}
	const FString FilePath = SlotToFilePath(Slot);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		return false;
	}
	int64 BodyOffset = 0;
	return ParseHeaderChunk(Bytes, OutHeader, BodyOffset) == EDP_SaveResult::Success;
}

FString UDP_SaveGameSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SaveGame: %d slot(s), %d pending async op(s), fmt v%d"),
		GetAllSlots().Num(), PendingOps, (int32)FDP_SaveVersion::LatestVersion);
}
