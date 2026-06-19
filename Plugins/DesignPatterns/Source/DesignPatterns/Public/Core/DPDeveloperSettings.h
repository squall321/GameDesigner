// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "DPDeveloperSettings.generated.h"

/** A pre-warmed pool the object-pool subsystem will create on world begin. */
USTRUCT(BlueprintType)
struct FDP_DefaultPoolConfig
{
	GENERATED_BODY()

	/** Class to pool (AActor subclass or plain UObject). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool")
	TSoftClassPtr<UObject> PooledClass;

	/** Instances to pre-create during warmup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0"))
	int32 InitialSize = 8;

	/** Soft cap; idle instances beyond this are eligible for eviction. 0 = unbounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0"))
	int32 SoftCap = 64;

	/** Whether the pool may grow past its current size when exhausted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool")
	bool bAllowGrow = true;

	/** Seconds an instance may sit idle past the soft cap before eviction. 0 = never evict. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0.0"))
	float IdleEvictSeconds = 30.f;
};

/**
 * Project-wide configuration for the DesignPatterns plugin. Appears under
 * Project Settings → Plugins → Design Patterns. Editing here requires no code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns"))
class DESIGNPATTERNS_API UDP_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDP_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Pools created automatically when the object-pool subsystem initializes for a world. */
	UPROPERTY(EditAnywhere, Config, Category = "Object Pool")
	TArray<FDP_DefaultPoolConfig> DefaultPools;

	/** Asset paths the data registry scans for UDP_DataAsset / data tables. */
	UPROPERTY(EditAnywhere, Config, Category = "Data Registry", meta = (LongPackageName))
	TArray<FDirectoryPath> DataRegistryScanPaths;

	/** Default save slot name used when callers don't specify one. */
	UPROPERTY(EditAnywhere, Config, Category = "Save System")
	FString DefaultSaveSlotName = TEXT("DPSave");

	/** Maximum entries kept in the command history ring buffer (undo/redo depth). */
	UPROPERTY(EditAnywhere, Config, Category = "Command", meta = (ClampMin = "0"))
	int32 CommandHistoryDepth = 128;

	/** When true, DP subsystems start with verbose logging enabled. */
	UPROPERTY(EditAnywhere, Config, Category = "Debug")
	bool bVerboseLoggingByDefault = false;

	/** Convenience accessor. */
	static const UDP_DeveloperSettings* Get();
};
