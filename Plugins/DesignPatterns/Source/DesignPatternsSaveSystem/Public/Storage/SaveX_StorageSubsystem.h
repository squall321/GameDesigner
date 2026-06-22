// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_SaveCipher.h"
#include "Storage/SaveX_ContainerHeader.h"
#include "SaveX_StorageSubsystem.generated.h"

class UDP_SaveGame;
class UDP_SaveGameSubsystem;
class UDP_ServiceLocatorSubsystem;
class USaveX_ThumbnailCapturer;

/** Outcome of a wrapped storage save/load. Widens EDP_SaveResult with wrapper-specific states. */
UENUM(BlueprintType)
enum class ESaveX_StorageResult : uint8
{
	/** The wrapped read/write completed successfully. */
	Success,
	/** A passed-in argument was invalid (empty slot, null object). */
	InvalidArgument,
	/** The core save subsystem could not be resolved (early load / no GameInstance). */
	SubsystemUnavailable,
	/** Obtaining/parsing the inner core blob failed (core serialize/deserialize error). */
	CoreFailure,
	/** Compression or decompression failed. */
	CompressionFailed,
	/** Encryption is required but no enabled cipher is available, or a cipher transform failed. */
	EncryptionFailed,
	/** On load: the container CRC did not match (corruption) and no recovery succeeded. */
	CorruptData,
	/** File IO (temp write, rename, read) failed. */
	IOFailed,
	/** The requested slot does not exist on disk. */
	SlotNotFound
};

/** Per-call native completion callback for a wrapped save. */
DECLARE_DELEGATE_TwoParams(FSaveX_StorageSaveDone, const FString& /*Slot*/, ESaveX_StorageResult /*Result*/);

/** Per-call native completion callback for a wrapped load; SaveObject null unless Result==Success. */
DECLARE_DELEGATE_ThreeParams(FSaveX_StorageLoadDone, const FString& /*Slot*/, ESaveX_StorageResult /*Result*/, UDP_SaveGame* /*SaveObject*/);

/** Broadcast (game thread) after a wrapped container is written; carries the on-disk path + cloud ETag. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSaveX_OnStorageWritten, FString, Slot, FString, ContainerFilePath, FString, ETag);

/** Broadcast (game thread) after a slot was recovered from a backup during load. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnStorageRecovered, FString, Slot, int32, BackupIndexUsed);

/**
 * ADDITIVE byte-buffer wrapper pipeline layered ON TOP of the core UDP_SaveGameSubsystem.
 *
 * It reinvents NO serialization: the core still owns the [hdr][body] blob format and all UObject
 * (de)serialization, which happens on the GAME THREAD. This subsystem wraps that opaque blob in a
 * corruption-safe, optionally compressed/encrypted container with an async thumbnail, and does the heavy
 * transform + file IO OFF the game thread on plain byte copies.
 *
 * WHY A SEPARATE EXTENSION: wrapped containers use ".dpcsav" with a distinct 'SAVX' magic so the core's
 * "*.dpsav" enumeration / ReadSlotHeader never sees them. A plain ".dpsav" with no SAVX magic is routed
 * straight to the core loader, so shipped (un-wrapped) saves keep working unchanged.
 *
 * OBTAINING THE CORE BLOB (the core's SerializeToBytes/DeserializeFromBytes are PRIVATE): this subsystem
 * wraps at the byte boundary it controls. On save it asks the core to write a reserved SCRATCH slot
 * (core SaveNow), reads the produced ".dpsav" bytes from disk, then deletes the scratch slot. On load it
 * writes the recovered inner bytes to a scratch ".dpsav" and asks the core to LoadNow it, then deletes the
 * scratch. The scratch slot uses a reserved prefix the slot manager already excludes from enumeration.
 *
 * THREADING: the core SaveNow/LoadNow + the scratch IO touch UObjects, so the BLOB acquisition happens on
 * the game thread. Only the compress/encrypt/CRC and the final atomic container write+rename+backup run on
 * a background task with plain byte copies. Load mirrors this: container read+verify+decrypt+decompress run
 * off-thread, and the core LoadNow (UObject work) runs back on the game thread.
 *
 * REPLICATION: none. This is a GameInstance subsystem holding no replicated state. Authority for which
 * world state is captured is enforced upstream by the slot manager's gather (authority-guarded) and the
 * ISeam_Persistable contract.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_StorageSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Serialize SaveObject through the core (game thread), then build + write a wrapped ".dpcsav" container
	 * (compress/encrypt/CRC/atomic-rename/backup off-thread). Captures a thumbnail first if policy requests
	 * one. OnDone + OnStorageWritten fire on the game thread.
	 *
	 * @param Slot         Target slot name (no extension).
	 * @param SaveObject   The save object to serialize (must be non-null).
	 * @param bIsAutosave  Threaded to the thumbnail policy (autosaves may skip thumbnails).
	 * @param ExtraFlags   Optional container flags to OR in (e.g. IsProfile / IsCheckpoint).
	 * @param OnDone       Optional native completion callback.
	 */
	void SaveWrapped(const FString& Slot, UDP_SaveGame* SaveObject, bool bIsAutosave,
		uint8 ExtraFlags = 0, FSaveX_StorageSaveDone OnDone = FSaveX_StorageSaveDone());

	/**
	 * Read + verify + decrypt + decompress a ".dpcsav" container (off-thread), then deserialize the inner
	 * core blob via the core (game thread). A plain ".dpsav" is routed straight to the core loader. On a
	 * corrupt primary, recovers from the newest valid backup when enabled. OnDone fires on the game thread.
	 */
	void LoadWrapped(const FString& Slot, FSaveX_StorageLoadDone OnDone = FSaveX_StorageLoadDone());

	/** Read just the container header of a wrapped slot (cheap; no payload transform). False if absent/plain. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool ReadContainerHeader(const FString& Slot, FSaveX_ContainerHeader& Out) const;

	/** True if a wrapped ".dpcsav" container exists for Slot. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DoesWrappedSlotExist(const FString& Slot) const;

	/**
	 * Attempt to restore Slot's primary container from its newest valid backup. Returns Success if a backup
	 * was promoted, SlotNotFound if no usable backup exists. Broadcasts OnStorageRecovered on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	ESaveX_StorageResult RecoverFromBackup(const FString& Slot);

	// ---- Path helpers (static; mirror the core's SlotToFilePath convention) ----

	/** File extension (no dot) for wrapped containers. */
	static const TCHAR* WrappedExtension() { return TEXT("dpcsav"); }

	/** Absolute path of the wrapped container for Slot (<Saved>/SaveGames/<Slot>.dpcsav). */
	static FString WrappedFilePath(const FString& Slot);

	/** Absolute path of the in-progress temp file used for the atomic write. */
	static FString TempFilePath(const FString& Slot);

	/** Absolute path of backup ring member Index for Slot (<Slot>.dpcsav.bak<Index>). */
	static FString BackupFilePath(const FString& Slot, int32 Index);

	// ---- Delegates ----

	/** Lets the cloud bridge (same module) bind upload-on-write. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FSaveX_OnStorageWritten OnStorageWritten;

	/** Broadcast when a slot's primary was recovered from a backup during load. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save")
	FSaveX_OnStorageRecovered OnStorageRecovered;

	/** The cipher seam applied to the byte buffer. Resolved at Initialize, re-resolved on demand; null = plaintext. */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_SaveCipher> Cipher;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the core save subsystem (the blob/header/IO owner). Null in early-load contexts. */
	UDP_SaveGameSubsystem* GetCoreSaveSubsystem() const;

	/** Resolve the service locator (for cipher resolution). */
	UDP_ServiceLocatorSubsystem* GetServiceLocator() const;

	/** Resolve (and cache) the cipher seam from the locator under the configured key. */
	void ResolveCipher();

	/** Reserved scratch slot name the core writes the raw blob to during wrap/unwrap (excluded from UI). */
	static FString ScratchSlotName(const FString& Slot);

	/**
	 * GAME THREAD: obtain the opaque core blob for SaveObject by routing it through the core to a reserved
	 * scratch ".dpsav", reading the bytes, then deleting the scratch slot. Returns true and fills OutBlob.
	 */
	bool AcquireCoreBlob_GameThread(const FString& Slot, UDP_SaveGame* SaveObject, TArray<uint8>& OutBlob) const;

	/**
	 * GAME THREAD: hand the recovered inner core blob back to the core for deserialization by writing it to
	 * a reserved scratch ".dpsav", calling core LoadNow, then deleting the scratch. Returns the loaded object.
	 */
	UDP_SaveGame* DeserializeCoreBlob_GameThread(const FString& Slot, const TArray<uint8>& InnerBlob, ESaveX_StorageResult& OutResult) const;

	/**
	 * OFF-THREAD-SAFE (operates on plain copies): transform InnerBlob -> on-disk container bytes
	 * (compress?->encrypt?->CRC->prepend header[+append thumbnail]). Fills OutHeader/OutFileBytes. Returns
	 * a result; EncryptionFailed/CompressionFailed on transform errors.
	 */
	ESaveX_StorageResult BuildContainerBytes(const TArray<uint8>& InnerBlob, uint8 ExtraFlags,
		const TArray<uint8>& ThumbnailPng, int32 ThumbW, int32 ThumbH,
		FSaveX_ContainerHeader& OutHeader, TArray<uint8>& OutFileBytes) const;

	/**
	 * OFF-THREAD-SAFE: parse FileBytes -> verify CRC -> decrypt -> decompress -> InnerBlob. Fills OutHeader.
	 * Returns CorruptData on CRC/parse failure, EncryptionFailed/CompressionFailed on transform failure.
	 */
	ESaveX_StorageResult ExtractInnerBlob(const TArray<uint8>& FileBytes,
		FSaveX_ContainerHeader& OutHeader, TArray<uint8>& OutInnerBlob) const;

	/**
	 * OFF-THREAD-SAFE: write FileBytes atomically (temp file + IFileManager::Move) and rotate the backup
	 * ring before promoting the temp file. Returns IOFailed on any IO error.
	 */
	ESaveX_StorageResult WriteContainerAtomic_OffThread(const FString& Slot, const TArray<uint8>& FileBytes) const;

	/** True if FileBytes begins with the wrapped SAVX magic (vs a plain core .dpsav with no container). */
	static bool LooksLikeWrappedContainer(const TArray<uint8>& FileBytes);

	/** Cached cipher service key (resolved from settings or the conventional fallback). */
	UPROPERTY(Transient)
	FGameplayTag CipherServiceKey;

	/** Owned helper that captures + encodes async screenshot thumbnails. */
	UPROPERTY(Transient)
	TObjectPtr<USaveX_ThumbnailCapturer> ThumbnailCapturer;

	/** Count of in-flight async writes/loads, surfaced in the debug string. */
	int32 PendingOps = 0;
};
