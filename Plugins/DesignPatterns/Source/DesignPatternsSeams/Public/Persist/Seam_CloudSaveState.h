// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_CloudSaveState.generated.h"

/**
 * Per-slot cloud sync status. Lives in the Seams module so both the Platform module (which produces
 * the state from its concrete SDK bridge) and the SaveSystem module (which consumes it to drive
 * upload-on-write / conflict-on-load) share one canonical enum.
 */
UENUM(BlueprintType)
enum class EPlat_CloudSyncState : uint8
{
	/** Not yet queried. */
	Unknown,
	/** A local file exists; the platform has no cloud backing (or cloud is unavailable). */
	LocalOnly,
	/** An upload/download is in flight. */
	Syncing,
	/** Local and remote agree. */
	Synced,
	/** Local and remote diverged; a resolver must pick a winner. */
	Conflict,
	/** The last cloud operation failed. */
	Error
};

/**
 * How a cloud-save conflict is resolved. Deliberately has NO "Merge" option: merging opaque save
 * bytes would require genre knowledge the framework cannot have, so merge is the project's job and is
 * expressed as KeepLocal-then-rewrite by the project's own logic if it wants it.
 */
UENUM(BlueprintType)
enum class ESeam_CloudResolution : uint8
{
	/** Keep the local file; overwrite remote on the next sync. */
	KeepLocal,
	/** Keep the remote file; overwrite local now. */
	KeepRemote,
	/** Defer the decision; the slot stays in Conflict until asked again (e.g. show UI later). */
	AskLater
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_CloudSaveStore : public UInterface
{
	GENERATED_BODY()
};

/**
 * Cloud-save STORE bridge, owned by the Platform module's UPlat_CloudSaveSubsystem (the concrete
 * platform-SDK wrapper, with a local-only fallback when no SDK is present). The SaveSystem's cloud
 * bridge consumes it through this seam — it uploads an already-ciphered, opaque save container and
 * queries remote state; it NEVER decrypts (encryption is owned entirely by the SaveSystem byte
 * pipeline via ISeam_SaveCipher). Resolved from the service locator under DP.Service.Save.Cloud.
 *
 * All methods are genre-agnostic: a slot is named by string; bytes are opaque; conflict detection is
 * a timestamp/size comparison only. Held weakly by the consumer; no-op fail-closed when unset.
 */
class DESIGNPATTERNSSEAMS_API ISeam_CloudSaveStore
{
	GENERATED_BODY()

public:
	/**
	 * Begin an asynchronous upload of the named slot's already-written local container to the cloud.
	 * Implementations move the slot to EPlat_CloudSyncState::Syncing and report completion through
	 * their own delegate. A no-op (returns false) when cloud is unavailable.
	 *
	 * @param SlotName  Logical slot name (unsanitised; the store sanitises/maps internally).
	 * @return True if an upload was started.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	bool UploadSlotAsync(const FString& SlotName);

	/**
	 * Query the current cloud sync state for a slot (cheap; returns the cached/last-known state).
	 * @param SlotName  Logical slot name.
	 * @return The slot's sync state, or LocalOnly/Unknown when cloud is unavailable.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	EPlat_CloudSyncState QueryRemoteState(const FString& SlotName) const;

	/**
	 * Compare local vs remote for a slot and report whether they have diverged. Comparison is by
	 * last-modified timestamp (and optionally size); the store fills the two times for a resolver.
	 *
	 * @param SlotName        Logical slot name.
	 * @param OutLocalTime    Local container's last-modified time (FDateTime::MinValue if absent).
	 * @param OutRemoteTime   Remote container's last-modified time (FDateTime::MinValue if absent).
	 * @return True if a conflict exists (both present and divergent); false otherwise.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	bool DetectConflict(const FString& SlotName, FDateTime& OutLocalTime, FDateTime& OutRemoteTime) const;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_CloudConflictResolver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Project-supplied hook that resolves a cloud-save conflict. Held OPTIONALLY (as a TScriptInterface,
 * no-op when unset) by UPlat_CloudSaveSubsystem. Genre-agnostic: it only sees the slot name and the
 * two timestamps and returns which side wins. When unset, the subsystem leaves the slot in Conflict.
 */
class DESIGNPATTERNSSEAMS_API ISeam_CloudConflictResolver
{
	GENERATED_BODY()

public:
	/**
	 * Decide how to resolve a conflict between the local and remote copies of a slot.
	 *
	 * @param SlotName    Logical slot name in conflict.
	 * @param LocalTime   Local container's last-modified time.
	 * @param RemoteTime  Remote container's last-modified time.
	 * @return KeepLocal / KeepRemote / AskLater.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	ESeam_CloudResolution ResolveConflict(const FString& SlotName, FDateTime LocalTime, FDateTime RemoteTime) const;
};
