// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Save/DPSaveHeader.h"
#include "DPSaveGameSubsystem.generated.h"

class UDP_SaveGame;
class UDP_SaveMigration;

/** Result of an async save/load operation. */
UENUM(BlueprintType)
enum class EDP_SaveResult : uint8
{
	/** Operation completed successfully. */
	Success,
	/** A passed-in argument was invalid (null object, empty slot...). */
	InvalidArgument,
	/** Serialization to/from bytes failed. */
	SerializationFailed,
	/** File IO (write/read) failed. */
	IOFailed,
	/** The blob was not a DP save (bad magic) or was truncated/corrupt. */
	CorruptData,
	/** The save's class could not be resolved/instantiated on load. */
	ClassResolutionFailed,
	/** Version migration failed. */
	MigrationFailed,
	/** The requested slot does not exist. */
	SlotNotFound
};

/** Fired (on the game thread) when an async save finishes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_SaveCompletedDynamic, FString, Slot, EDP_SaveResult, Result);
/** Fired (on the game thread) when an async load finishes; SaveObject is null unless Result==Success. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDP_LoadCompletedDynamic, FString, Slot, EDP_SaveResult, Result, UDP_SaveGame*, SaveObject);

/** Per-call completion delegates (BP-assignable, one-shot). */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FDP_SaveCallbackDynamic, FString, Slot, EDP_SaveResult, Result);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FDP_LoadCallbackDynamic, FString, Slot, EDP_SaveResult, Result, UDP_SaveGame*, SaveObject);

/**
 * Owns the DesignPatterns save file format and async save/load lifecycle.
 *
 * Threading contract (the important part): the SaveGame object is gathered (OnPreSave) and
 * SERIALIZED TO A BYTE BUFFER ON THE GAME THREAD. Only the resulting raw byte array is handed
 * to a background task for the actual file write — no UObject is ever touched off the game
 * thread. Load mirrors this: bytes are read on a background task, then ALL UObject work
 * (header parse, class resolution, deserialize, migrate, OnPostLoad, delegate) happens back on
 * the game thread.
 *
 * Blob layout: [int64 HeaderChunkLen][FDP_SaveHeader bytes][int64 BodyChunkLen][SaveGame body bytes].
 * The header is a separate length-prefixed chunk so DP.Save.DumpHeader can read metadata cheaply.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_SaveGameSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Serialize SaveObject (on the game thread) and write the bytes to Slot on a background
	 * task. OnComplete and the OnSaveCompleted multicast fire on the game thread when done.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save", meta = (AutoCreateRefTerm = "OnComplete"))
	void SaveAsync(const FString& Slot, UDP_SaveGame* SaveObject, const FDP_SaveCallbackDynamic& OnComplete);

	/**
	 * Read Slot's bytes on a background task, then (on the game thread) parse, instantiate,
	 * deserialize, migrate and OnPostLoad. OnComplete and OnLoadCompleted fire on the game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save", meta = (AutoCreateRefTerm = "OnComplete"))
	void LoadAsync(const FString& Slot, const FDP_LoadCallbackDynamic& OnComplete);

	/** Synchronous save (game thread, blocking). Backing for DP.Save.SaveNow. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	EDP_SaveResult SaveNow(const FString& Slot, UDP_SaveGame* SaveObject);

	/** Synchronous load (game thread, blocking). Backing for DP.Save.LoadNow. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	UDP_SaveGame* LoadNow(const FString& Slot, EDP_SaveResult& OutResult);

	/** Delete a slot's file. Returns true if a file existed and was removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DeleteSlot(const FString& Slot);

	/** True if a save file exists for Slot. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DoesSlotExist(const FString& Slot) const;

	/** Enumerate all DP save slot names in the save directory (no extension). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	TArray<FString> GetAllSlots() const;

	/**
	 * Read just the header chunk of Slot without deserializing the body. Returns false if the
	 * slot is missing or corrupt. Backing for DP.Save.DumpHeader.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool ReadSlotHeader(const FString& Slot, FDP_SaveHeader& OutHeader) const;

	/** The migration registry. Register UDP_SaveMigrationStep instances here at startup. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	UDP_SaveMigration* GetMigration() const { return Migration; }

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast when any async/sync save finishes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FDP_SaveCompletedDynamic OnSaveCompleted;

	/** Broadcast when any async/sync load finishes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FDP_LoadCompletedDynamic OnLoadCompleted;

private:
	/** Owned migration registry (instanced subobject). */
	UPROPERTY()
	TObjectPtr<UDP_SaveMigration> Migration;

	/** Count of in-flight async operations (for debug string / safe-shutdown awareness). */
	int32 PendingOps = 0;

	/** Resolve "Slot" -> absolute file path under the project's SaveGames directory. */
	static FString SlotToFilePath(const FString& Slot);

	/** File extension for DP saves (without dot). */
	static const TCHAR* SaveExtension() { return TEXT("dpsav"); }

	/**
	 * GAME-THREAD: gather + serialize SaveObject into a self-describing byte blob.
	 * Returns Success and fills OutBytes, or a failure result.
	 */
	EDP_SaveResult SerializeToBytes(UDP_SaveGame* SaveObject, TArray<uint8>& OutBytes) const;

	/**
	 * GAME-THREAD: parse a blob's header (and, if bDeserializeBody, the body) into a
	 * newly-constructed UDP_SaveGame, running migration + OnPostLoad. OutSave is null on failure.
	 */
	EDP_SaveResult DeserializeFromBytes(const TArray<uint8>& Bytes, bool bDeserializeBody,
		FDP_SaveHeader& OutHeader, UDP_SaveGame*& OutSave) const;

	/** Parse only the header chunk from a blob (no body, no UObject construction). */
	static EDP_SaveResult ParseHeaderChunk(const TArray<uint8>& Bytes, FDP_SaveHeader& OutHeader, int64& OutBodyOffset);
};
