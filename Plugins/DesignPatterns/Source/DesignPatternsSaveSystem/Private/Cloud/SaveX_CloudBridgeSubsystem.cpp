// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Cloud/SaveX_CloudBridgeSubsystem.h"
#include "Storage/SaveX_StorageSubsystem.h"
#include "Storage/SaveX_ContainerHeader.h"
#include "Settings/SaveX_StorageDeveloperSettings.h"
#include "SaveX_StorageServiceKeys.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"          // FDP_SubsystemStatics
#include "Services/DPServiceLocatorSubsystem.h"

#include "Misc/FileHelper.h"

// The cloud store seam is owned + implemented by the Platform module but DEFINED in DesignPatternsSeams.
// Guard the include so this module compiles even before the Platform seam lands; when present, the bridge
// drives the real seam, otherwise it degrades to an inert (Unavailable) bridge.
#if __has_include("Persist/Seam_CloudSaveState.h")
	#include "Persist/Seam_CloudSaveState.h"
	#define SAVEX_HAS_CLOUD_SEAM 1
#else
	#define SAVEX_HAS_CLOUD_SEAM 0
#endif

void USaveX_CloudBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get())
	{
		CloudServiceKey = Settings->CloudServiceTag.IsValid() ? Settings->CloudServiceTag : SaveX_StorageServiceKeys::Cloud();
		bUploadOnSave = Settings->bUploadToCloudOnSave;
	}
	else
	{
		CloudServiceKey = SaveX_StorageServiceKeys::Cloud();
	}

	// Bind upload-on-write (intra-module delegate from the storage subsystem).
	if (USaveX_StorageSubsystem* Storage = GetStorage())
	{
		Storage->OnStorageWritten.AddDynamic(this, &USaveX_CloudBridgeSubsystem::HandleStorageWritten);
	}

	UE_LOG(LogDPSave, Log, TEXT("[Cloud] Initialized (uploadOnSave=%s, seamCompiled=%d)."),
		bUploadOnSave ? TEXT("yes") : TEXT("no"), SAVEX_HAS_CLOUD_SEAM);
}

void USaveX_CloudBridgeSubsystem::Deinitialize()
{
	// Unbind the storage delegate so a late broadcast never reaches a torn-down bridge.
	if (USaveX_StorageSubsystem* Storage = GetStorage())
	{
		Storage->OnStorageWritten.RemoveDynamic(this, &USaveX_CloudBridgeSubsystem::HandleStorageWritten);
	}
	Super::Deinitialize();
}

USaveX_StorageSubsystem* USaveX_CloudBridgeSubsystem::GetStorage() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<USaveX_StorageSubsystem>(
		const_cast<USaveX_CloudBridgeSubsystem*>(this));
}

UDP_ServiceLocatorSubsystem* USaveX_CloudBridgeSubsystem::GetServiceLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(
		const_cast<USaveX_CloudBridgeSubsystem*>(this));
}

UObject* USaveX_CloudBridgeSubsystem::ResolveCloudStore() const
{
	if (!CloudServiceKey.IsValid())
	{
		return nullptr;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = GetServiceLocator())
	{
		return Locator->ResolveService(CloudServiceKey);
	}
	return nullptr;
}

void USaveX_CloudBridgeSubsystem::HandleStorageWritten(FString Slot, FString /*ContainerFilePath*/, FString /*ETag*/)
{
	if (!bUploadOnSave)
	{
		return;
	}

	UObject* Store = ResolveCloudStore();
	if (!Store)
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Cloud] No cloud store bound; skipping upload of '%s'."), *Slot);
		return;
	}

	++UploadsAttempted;

#if SAVEX_HAS_CLOUD_SEAM
	if (Store->GetClass()->ImplementsInterface(USeam_CloudSaveStore::StaticClass()))
	{
		// The store reads the already-written, opaque (already-ciphered) local container itself — the seam
		// is named by slot only, so the cloud bridge never hands raw bytes across the boundary and never
		// decrypts. The store begins an async upload and reports completion through its own delegate.
		const bool bStarted = ISeam_CloudSaveStore::Execute_UploadSlotAsync(Store, Slot);
		UE_CLOG(bStarted, LogDPSave, Log, TEXT("[Cloud] Upload started for '%s'."), *Slot);
		UE_CLOG(!bStarted, LogDPSave, Verbose, TEXT("[Cloud] Cloud unavailable; upload of '%s' not started."), *Slot);
		return;
	}
#endif

	UE_LOG(LogDPSave, Verbose, TEXT("[Cloud] Cloud seam unavailable at compile/runtime; upload of '%s' skipped."), *Slot);
}

ESaveX_CloudConflictState USaveX_CloudBridgeSubsystem::CheckConflict(const FString& Slot) const
{
	UObject* Store = ResolveCloudStore();
	if (!Store)
	{
		return ESaveX_CloudConflictState::Unavailable;
	}

#if SAVEX_HAS_CLOUD_SEAM
	if (Store->GetClass()->ImplementsInterface(USeam_CloudSaveStore::StaticClass()))
	{
		// The store compares local vs remote by last-modified timestamp and reports divergence directly.
		FDateTime LocalTime = FDateTime::MinValue();
		FDateTime RemoteTime = FDateTime::MinValue();
		const bool bConflict = ISeam_CloudSaveStore::Execute_DetectConflict(Store, Slot, LocalTime, RemoteTime);
		return bConflict ? ESaveX_CloudConflictState::Conflict : ESaveX_CloudConflictState::NoConflict;
	}
#endif

	return ESaveX_CloudConflictState::Unavailable;
}

void USaveX_CloudBridgeSubsystem::ResolveConflict(const FString& Slot, ESaveX_CloudResolutionChoice Resolution)
{
	UObject* Store = ResolveCloudStore();
	if (!Store)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Cloud] ResolveConflict('%s'): no cloud store."), *Slot);
		return;
	}

	switch (Resolution)
	{
	case ESaveX_CloudResolutionChoice::KeepLocal:
	{
		// Re-upload the local container, overwriting remote on the next sync.
#if SAVEX_HAS_CLOUD_SEAM
		if (Store->GetClass()->ImplementsInterface(USeam_CloudSaveStore::StaticClass()))
		{
			ISeam_CloudSaveStore::Execute_UploadSlotAsync(Store, Slot);
		}
#endif
		UE_LOG(LogDPSave, Log, TEXT("[Cloud] Conflict on '%s' resolved: KeepLocal (re-upload)."), *Slot);
		break;
	}
	case ESaveX_CloudResolutionChoice::KeepRemote:
		// Remote download is the platform store's responsibility; the project's store impl performs the
		// fetch+overwrite when asked. We log intent here; the concrete download is platform-specific.
		UE_LOG(LogDPSave, Log, TEXT("[Cloud] Conflict on '%s' resolved: KeepRemote (download pending in store impl)."), *Slot);
		break;
	case ESaveX_CloudResolutionChoice::AskLater:
		UE_LOG(LogDPSave, Verbose, TEXT("[Cloud] Conflict on '%s' deferred (AskLater)."), *Slot);
		break;
	}
}

FString USaveX_CloudBridgeSubsystem::GetDPDebugString_Implementation() const
{
	const bool bStore = ResolveCloudStore() != nullptr;
	return FString::Printf(TEXT("Cloud: store=%s uploadOnSave=%s uploads=%d seam=%d"),
		bStore ? TEXT("bound") : TEXT("none"),
		bUploadOnSave ? TEXT("yes") : TEXT("no"),
		UploadsAttempted, SAVEX_HAS_CLOUD_SEAM);
}
