// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Storage/UPlat_StorageLibrary.h"
#include "Persist/Seam_CloudSaveState.h"
#include "UPlat_CloudStorageLibrary.generated.h"

/**
 * Pure cloud-state query helpers, sitting beside UPlat_StorageLibrary (which it does not modify).
 * Reuses FPlat_SavePath::bCloudSaveAware so callers can branch on cloud backing without touching the
 * cloud subsystem.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_CloudStorageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when the resolved save path is backed by cloud/roaming sync. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Cloud")
	static bool IsCloudBacked(const FPlat_SavePath& Path);

	/**
	 * The current cloud sync state for a slot, read from the Platform cloud subsystem via a world
	 * context. Returns LocalOnly when the subsystem is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Cloud", meta = (WorldContext = "WorldContextObject"))
	static EPlat_CloudSyncState GetSlotState(const UObject* WorldContextObject, const FString& Slot);
};
