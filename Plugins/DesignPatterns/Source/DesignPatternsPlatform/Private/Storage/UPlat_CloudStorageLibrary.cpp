// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/UPlat_CloudStorageLibrary.h"
#include "Storage/UPlat_CloudSaveSubsystem.h"
#include "Core/DPSubsystemLibrary.h"

bool UPlat_CloudStorageLibrary::IsCloudBacked(const FPlat_SavePath& Path)
{
	return Path.bCloudSaveAware;
}

EPlat_CloudSyncState UPlat_CloudStorageLibrary::GetSlotState(const UObject* WorldContextObject, const FString& Slot)
{
	if (UPlat_CloudSaveSubsystem* Cloud = FDP_SubsystemStatics::GetGameInstanceSubsystem<UPlat_CloudSaveSubsystem>(WorldContextObject))
	{
		return Cloud->GetSlotStatus(Slot).State;
	}
	return EPlat_CloudSyncState::LocalOnly;
}
