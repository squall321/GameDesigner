// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/UPlat_CloudSaveSubsystem.h"
#include "Storage/UPlat_StorageLibrary.h"
#include "Plat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	/**
	 * The cloud-store service key is owned by the SaveSystem module (DP.Service.Save.Cloud). We resolve
	 * it at runtime (not declared as a native tag here) to avoid a duplicate native-tag definition. The
	 * tag string must stay in sync with SaveX_StorageServiceKeys.h.
	 */
	FGameplayTag GetCloudServiceKey()
	{
		static const FGameplayTag Key = FGameplayTag::RequestGameplayTag(FName("DP.Service.Save.Cloud"), /*ErrorIfNotFound=*/false);
		return Key;
	}
}

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UPlat_CloudSaveSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// A dedicated server has no player cloud storage; skip creation.
	return !IsRunningDedicatedServer();
}

void UPlat_CloudSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterCloudService();
	UE_LOG(LogDP, Log, TEXT("[Platform] CloudSaveSubsystem initialized (cloudAware=%d)."),
		UPlat_StorageLibrary::IsCloudSaveAware() ? 1 : 0);
}

void UPlat_CloudSaveSubsystem::Deinitialize()
{
	UnregisterCloudService();
	Slots.Empty();
	Resolver = nullptr;
	Super::Deinitialize();
}

FString UPlat_CloudSaveSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("CloudSave slots=%d resolver=%s cloudAware=%d"),
		Slots.Num(),
		Resolver.GetObject() ? TEXT("set") : TEXT("none"),
		UPlat_StorageLibrary::IsCloudSaveAware() ? 1 : 0);
}

// ---------------------------------------------------------------------------------------------
//  Service registration
// ---------------------------------------------------------------------------------------------

void UPlat_CloudSaveSubsystem::RegisterCloudService()
{
	if (bRegisteredService)
	{
		return;
	}
	const FGameplayTag Key = GetCloudServiceKey();
	if (!Key.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Platform] Cloud service key DP.Service.Save.Cloud not registered; cloud store not published."));
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		bRegisteredService = Locator->RegisterService(
			Key, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UPlat_CloudSaveSubsystem::UnregisterCloudService()
{
	if (!bRegisteredService)
	{
		return;
	}
	const FGameplayTag Key = GetCloudServiceKey();
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Key.IsValid() && Locator->ResolveService(Key) == this)
		{
			Locator->UnregisterService(Key);
		}
	}
	bRegisteredService = false;
}

// ---------------------------------------------------------------------------------------------
//  Local status helpers
// ---------------------------------------------------------------------------------------------

FPlat_CloudSlotStatus UPlat_CloudSaveSubsystem::BuildLocalStatus(const FString& SlotName) const
{
	FPlat_CloudSlotStatus Status;
	// Reuse the shipped storage library to resolve the platform-correct path (we never re-derive it).
	Status.SavePath = UPlat_StorageLibrary::ResolveSavePath(SlotName);

	IFileManager& FM = IFileManager::Get();
	const FString& FullPath = Status.SavePath.FullPath;
	if (!FullPath.IsEmpty() && FM.FileExists(*FullPath))
	{
		Status.LocalModified = FM.GetTimeStamp(*FullPath);
		Status.LocalSizeBytes = FM.FileSize(*FullPath);
	}

	// Cloud awareness comes from the platform path resolution; absent cloud => LocalOnly.
	Status.State = Status.SavePath.bCloudSaveAware ? EPlat_CloudSyncState::Unknown : EPlat_CloudSyncState::LocalOnly;
	return Status;
}

void UPlat_CloudSaveSubsystem::SetSlotState(const FString& SlotName, EPlat_CloudSyncState NewState)
{
	FPlat_CloudSlotStatus& Status = Slots.FindOrAdd(SlotName);
	if (Status.State != NewState)
	{
		Status.State = NewState;
		OnCloudStateChanged.Broadcast(SlotName, NewState);
	}
}

// ---------------------------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------------------------

void UPlat_CloudSaveSubsystem::RefreshSlotStatus(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		return;
	}

	FPlat_CloudSlotStatus Local = BuildLocalStatus(SlotName);

	// Query remote through the platform bridge (this is the local fallback path unless a real SDK is
	// wired in below). On generic platforms there is no remote, so we stay LocalOnly.
	FDateTime RemoteTime = FDateTime::MinValue();
	int64 RemoteSize = 0;
	bool bRemotePresent = false;

#if WITH_CLOUD_SAVE_SDK
	// Platform SDK bridge would populate RemoteTime/RemoteSize/bRemotePresent here. Confined to the
	// platform extension so the base module compiles without an SDK.
#endif

	Local.RemoteModified = RemoteTime;
	Local.RemoteSizeBytes = RemoteSize;

	// Decide state.
	EPlat_CloudSyncState NewState = Local.State;
	if (!Local.SavePath.bCloudSaveAware || !bRemotePresent)
	{
		NewState = EPlat_CloudSyncState::LocalOnly;
	}
	else
	{
		const bool bLocalPresent = Local.LocalSizeBytes > 0;
		if (bLocalPresent && bRemotePresent && Local.LocalModified != Local.RemoteModified)
		{
			NewState = EPlat_CloudSyncState::Conflict;
		}
		else
		{
			NewState = EPlat_CloudSyncState::Synced;
		}
	}
	Local.State = NewState;

	Slots.Add(SlotName, Local);
	// Broadcast via SetSlotState path (re-set to fire change detection against any prior cached value).
	OnCloudStateChanged.Broadcast(SlotName, NewState);

	// On conflict, consult the optional resolver immediately.
	if (NewState == EPlat_CloudSyncState::Conflict && Resolver.GetObject())
	{
		const ESeam_CloudResolution Decision =
			ISeam_CloudConflictResolver::Execute_ResolveConflict(
				Resolver.GetObject(), SlotName, Local.LocalModified, Local.RemoteModified);
		ApplyResolution(SlotName, Decision);
	}
}

FPlat_CloudSlotStatus UPlat_CloudSaveSubsystem::GetSlotStatus(const FString& SlotName) const
{
	if (const FPlat_CloudSlotStatus* Found = Slots.Find(SlotName))
	{
		return *Found;
	}
	return BuildLocalStatus(SlotName);
}

void UPlat_CloudSaveSubsystem::RequestSync(const FString& SlotName)
{
	UploadSlotAsync_Implementation(SlotName);
}

void UPlat_CloudSaveSubsystem::SetConflictResolver(const TScriptInterface<ISeam_CloudConflictResolver>& InResolver)
{
	Resolver = InResolver;
}

void UPlat_CloudSaveSubsystem::ApplyResolution(const FString& SlotName, ESeam_CloudResolution Resolution)
{
	switch (Resolution)
	{
	case ESeam_CloudResolution::KeepLocal:
		// Local wins: queue an upload so remote matches local.
		SetSlotState(SlotName, EPlat_CloudSyncState::Syncing);
		UploadSlotAsync_Implementation(SlotName);
		break;

	case ESeam_CloudResolution::KeepRemote:
		// Remote wins: a real SDK would download here; generic fallback marks Synced (no remote).
		SetSlotState(SlotName, EPlat_CloudSyncState::Synced);
		break;

	case ESeam_CloudResolution::AskLater:
	default:
		// Leave the slot in Conflict; UI will ask again later.
		break;
	}
}

// ---------------------------------------------------------------------------------------------
//  ISeam_CloudSaveStore
// ---------------------------------------------------------------------------------------------

bool UPlat_CloudSaveSubsystem::UploadSlotAsync_Implementation(const FString& SlotName)
{
	if (SlotName.IsEmpty())
	{
		return false;
	}

	const FPlat_SavePath Path = UPlat_StorageLibrary::ResolveSavePath(SlotName);
	if (!Path.bCloudSaveAware)
	{
		// No cloud backing on this platform: nothing to upload, slot is LocalOnly.
		SetSlotState(SlotName, EPlat_CloudSyncState::LocalOnly);
		return false;
	}

	if (!IFileManager::Get().FileExists(*Path.FullPath))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Platform] UploadSlotAsync: no local file for slot %s."), *SlotName);
		return false;
	}

	SetSlotState(SlotName, EPlat_CloudSyncState::Syncing);

#if WITH_CLOUD_SAVE_SDK
	// Real platform upload of the already-ciphered opaque container goes here; on completion the bridge
	// calls SetSlotState(SlotName, Synced/Error). Confined to the platform extension.
	return true;
#else
	// Generic fallback: cloud-aware directory but no SDK to push to (e.g. an OS-roaming folder that the
	// platform syncs transparently). Treat the local write as already synced.
	SetSlotState(SlotName, EPlat_CloudSyncState::Synced);
	return true;
#endif
}

EPlat_CloudSyncState UPlat_CloudSaveSubsystem::QueryRemoteState_Implementation(const FString& SlotName) const
{
	if (const FPlat_CloudSlotStatus* Found = Slots.Find(SlotName))
	{
		return Found->State;
	}
	return UPlat_StorageLibrary::IsCloudSaveAware() ? EPlat_CloudSyncState::Unknown : EPlat_CloudSyncState::LocalOnly;
}

bool UPlat_CloudSaveSubsystem::DetectConflict_Implementation(const FString& SlotName, FDateTime& OutLocalTime, FDateTime& OutRemoteTime) const
{
	const FPlat_CloudSlotStatus Status = GetSlotStatus(SlotName);
	OutLocalTime = Status.LocalModified;
	OutRemoteTime = Status.RemoteModified;

	const bool bBothPresent = (Status.LocalModified != FDateTime::MinValue()) && (Status.RemoteModified != FDateTime::MinValue());
	return bBothPresent && (Status.LocalModified != Status.RemoteModified);
}
