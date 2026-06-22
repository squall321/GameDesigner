// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "SaveX_CloudBridgeSubsystem.generated.h"

class USaveX_StorageSubsystem;
class UDP_ServiceLocatorSubsystem;

/**
 * Local mirror of the cloud conflict outcome so this header compiles whether or not the Platform-owned
 * cloud seam (Persist/Seam_CloudSaveState.h, which defines EPlat_CloudSyncState / ESeam_CloudResolution)
 * is present in the build yet. When the seam header IS available the bridge maps the seam's enum onto this
 * one; when it is NOT, the bridge degrades to NoConflict (inert).
 */
UENUM(BlueprintType)
enum class ESaveX_CloudConflictState : uint8
{
	/** Cloud store unavailable or not configured. */
	Unavailable,
	/** Local and remote agree (or no remote exists); safe to proceed. */
	NoConflict,
	/** Remote differs from local; the player/UI must choose which to keep. */
	Conflict
};

/** How a detected conflict should be resolved (mirrors the seam's ESeam_CloudResolution). */
UENUM(BlueprintType)
enum class ESaveX_CloudResolutionChoice : uint8
{
	/** Keep the local save (re-upload it, overwriting remote). */
	KeepLocal,
	/** Keep the remote save (re-download it, overwriting local). */
	KeepRemote,
	/** Defer; leave both untouched for now. */
	AskLater
};

/** Broadcast (game thread) when a cloud-vs-local conflict is detected on load. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnCloudConflict, FString, Slot, ESaveX_CloudConflictState, State);

/**
 * Drives the Platform-owned cloud save store seam from the SaveSystem side: upload-on-write and
 * conflict-detect-on-load.
 *
 * It binds USaveX_StorageSubsystem::OnStorageWritten (same module — intra-module delegate, allowed) to
 * upload the opaque, already-ciphered container bytes. The cloud never decrypts; it stores opaque bytes.
 * On load resolution it reads the local container header's timestamp/ETag and asks the store seam to
 * detect a conflict, broadcasting OnCloudConflictDetected for a UI prompt.
 *
 * DECOUPLING: the cloud store seam lives in DesignPatternsSeams and is IMPLEMENTED by the Platform module.
 * This subsystem resolves it by service-locator tag as a weak intent (TScriptInterface), never hard-
 * including a platform SDK. It holds NO replicated state.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_CloudBridgeSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Detect whether the given slot is in conflict with the cloud (Unavailable when no store is bound). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Cloud")
	ESaveX_CloudConflictState CheckConflict(const FString& Slot) const;

	/** Apply a resolution to a detected conflict (re-upload local, or trigger a re-download of remote). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Cloud")
	void ResolveConflict(const FString& Slot, ESaveX_CloudResolutionChoice Resolution);

	/** Broadcast when a conflict is detected on load. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Cloud")
	FSaveX_OnCloudConflict OnCloudConflictDetected;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the storage subsystem (for binding OnStorageWritten + reading container headers). */
	USaveX_StorageSubsystem* GetStorage() const;

	/** Resolve the service locator. */
	UDP_ServiceLocatorSubsystem* GetServiceLocator() const;

	/** Resolve the cloud store seam provider object from the locator (null when unbound). */
	UObject* ResolveCloudStore() const;

	/** Bound to USaveX_StorageSubsystem::OnStorageWritten: uploads the container bytes to the cloud. */
	UFUNCTION()
	void HandleStorageWritten(FString Slot, FString ContainerFilePath, FString ETag);

	/** Cached cloud service key (from settings or the conventional fallback). */
	UPROPERTY(Transient)
	FGameplayTag CloudServiceKey;

	/** True if upload-on-write is enabled (mirrors the storage settings). */
	bool bUploadOnSave = false;

	/** Count of uploads attempted, for the debug string. */
	int32 UploadsAttempted = 0;
};
