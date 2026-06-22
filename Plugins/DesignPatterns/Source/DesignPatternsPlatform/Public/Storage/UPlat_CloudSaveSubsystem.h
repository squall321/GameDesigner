// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_CloudSaveState.h"
#include "Storage/UPlat_CloudTypes.h"
#include "UObject/ScriptInterface.h"
#include "UPlat_CloudSaveSubsystem.generated.h"

class ISeam_CloudConflictResolver;

/** Broadcast when a slot's cloud sync state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPlat_OnCloudStateChanged, const FString&, SlotName, EPlat_CloudSyncState, NewState);

/**
 * Tracks per-slot cloud sync state, WRAPPING the platform cloud user-storage interface behind #ifdef
 * with a local-only fallback (state = LocalOnly) so the base module always compiles. Composes with
 * UPlat_StorageLibrary::ResolveSavePath to find the local container (it never modifies that library).
 *
 * Implements ISeam_CloudSaveStore (consumed by the SaveSystem's cloud bridge to upload an already-
 * ciphered, opaque container and query/detect remote state — this subsystem NEVER decrypts) and
 * self-registers under DP.Service.Save.Cloud (WeakObserved). On a detected Conflict it consults the
 * OPTIONAL project-supplied ISeam_CloudConflictResolver (held as a TScriptInterface) and applies
 * KeepLocal / KeepRemote; AskLater leaves the slot in Conflict. No replication. Skipped on dedicated
 * servers.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_CloudSaveSubsystem : public UDP_GameInstanceSubsystem, public ISeam_CloudSaveStore
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast when any slot's sync state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Cloud")
	FPlat_OnCloudStateChanged OnCloudStateChanged;

	/**
	 * Re-resolve the local path + cloud state for a slot and cache it. Cheap; safe to poll. Detects a
	 * conflict and, when a resolver is set, applies its decision.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Cloud")
	void RefreshSlotStatus(const FString& SlotName);

	/** The last-cached status for a slot (LocalOnly/Unknown if never refreshed). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Cloud")
	FPlat_CloudSlotStatus GetSlotStatus(const FString& SlotName) const;

	/** Request an upload of the slot's local container to the cloud (no-op when cloud is unavailable). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Cloud")
	void RequestSync(const FString& SlotName);

	/** Set (or clear) the optional project conflict resolver. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Cloud")
	void SetConflictResolver(const TScriptInterface<ISeam_CloudConflictResolver>& InResolver);

	//~ Begin ISeam_CloudSaveStore
	virtual bool UploadSlotAsync_Implementation(const FString& SlotName) override;
	virtual EPlat_CloudSyncState QueryRemoteState_Implementation(const FString& SlotName) const override;
	virtual bool DetectConflict_Implementation(const FString& SlotName, FDateTime& OutLocalTime, FDateTime& OutRemoteTime) const override;
	//~ End ISeam_CloudSaveStore

private:
	/** Set a slot's state and broadcast if it changed. */
	void SetSlotState(const FString& SlotName, EPlat_CloudSyncState NewState);

	/** Resolve and stat the local container for a slot into a status record. */
	FPlat_CloudSlotStatus BuildLocalStatus(const FString& SlotName) const;

	/** Apply a resolver decision to a conflicted slot (KeepLocal/KeepRemote/AskLater). */
	void ApplyResolution(const FString& SlotName, ESeam_CloudResolution Resolution);

	/** Register/unregister the cloud-store service. */
	void RegisterCloudService();
	void UnregisterCloudService();

	/** Optional project conflict resolver (no-op when unset). */
	UPROPERTY()
	TScriptInterface<ISeam_CloudConflictResolver> Resolver;

	/** Per-slot cached status, keyed by the raw (unsanitised) slot name the caller passed. */
	UPROPERTY()
	TMap<FString, FPlat_CloudSlotStatus> Slots;

	/** True once the service was registered. */
	bool bRegisteredService = false;
};
