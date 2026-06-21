// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_SaveSlotManager.generated.h"

/** Lightweight metadata about a named save slot (for a save/load UI and "continue"). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_SaveSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Save")
	FString SlotName;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Save")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Save")
	FDateTime Timestamp = FDateTime(0);

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Save")
	float PlaytimeSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Save")
	bool bExists = false;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_SaveSlotManager : public UInterface
{
	GENERATED_BODY()
};

/**
 * Named save-slot policy seam over the core save subsystem. The SaveSystem module implements it; the
 * game-flow "continue" feature and any module needing slot metadata read it without inventing their own
 * slot bookkeeping. The actual byte/header/async-IO machinery stays in the core UDP_SaveGameSubsystem.
 */
class DESIGNPATTERNSSEAMS_API ISeam_SaveSlotManager
{
	GENERATED_BODY()

public:
	/** Metadata for every known slot. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Save")
	void GetAllSlots(TArray<FSeam_SaveSlotInfo>& OutSlots) const;

	/** The most-recently-written slot name (empty if none) — backs a "Continue" button. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Save")
	FString GetMostRecentSlot() const;

	/** True if a slot with this name exists on disk. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Save")
	bool DoesSlotExist(const FString& SlotName) const;
};
