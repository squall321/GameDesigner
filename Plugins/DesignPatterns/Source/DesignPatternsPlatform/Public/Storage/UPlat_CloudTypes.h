// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Storage/UPlat_StorageLibrary.h"
#include "Persist/Seam_CloudSaveState.h"
#include "UPlat_CloudTypes.generated.h"

/**
 * Cloud sync status for one save slot. Reuses the real FPlat_SavePath (and its bCloudSaveAware flag)
 * so the cloud layer composes with the shipped storage library instead of re-deriving paths. Plain
 * BlueprintReadOnly value type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_CloudSlotStatus
{
	GENERATED_BODY()

	/** The resolved on-disk path for this slot (from UPlat_StorageLibrary::ResolveSavePath). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	FPlat_SavePath SavePath;

	/** Current sync state (shared enum from the cloud seam). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	EPlat_CloudSyncState State = EPlat_CloudSyncState::Unknown;

	/** Local container's last-modified time (MinValue if there is no local file). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	FDateTime LocalModified = FDateTime::MinValue();

	/** Remote container's last-modified time (MinValue if there is no remote copy / no cloud). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	FDateTime RemoteModified = FDateTime::MinValue();

	/** Local container size in bytes (0 if absent). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	int64 LocalSizeBytes = 0;

	/** Remote container size in bytes (0 if absent / unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Cloud")
	int64 RemoteSizeBytes = 0;
};
