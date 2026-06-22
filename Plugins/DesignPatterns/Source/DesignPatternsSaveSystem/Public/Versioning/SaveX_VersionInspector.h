// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Persist/Seam_SaveSlotManager.h" // FSeam_SaveSlotInfo
#include "SaveX_VersionInspector.generated.h"

class UDP_SaveGameSubsystem;
class USaveX_StorageSubsystem;

/** Compatibility classification of a save slot relative to the current build's save format. */
UENUM(BlueprintType)
enum class ESaveX_VersionStatus : uint8
{
	/** Header readable and SaveVersion == LatestVersion; load directly. */
	Current,
	/** Header readable and SaveVersion < LatestVersion but >= oldest supported; migration will run on load. */
	NeedsMigration,
	/** SaveVersion > LatestVersion: written by a NEWER build; this build cannot safely load it. */
	IncompatibleNewer,
	/** SaveVersion < oldest supported: too old for the current migration chain. */
	IncompatibleOlder,
	/** Header unreadable / bad magic / failed container CRC: corrupt. */
	Corrupt,
	/** No file exists for this slot. */
	Missing
};

/** Per-slot version classification surfaced to a save-management UX. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSAVESYSTEM_API FSaveX_SlotVersionInfo
{
	GENERATED_BODY()

	/** Slot name (no extension). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FString SlotName;

	/** Player-facing label from the header (or the slot name if blank). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	FText DisplayName;

	/** The classification. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	ESaveX_VersionStatus Status = ESaveX_VersionStatus::Missing;

	/** The FDP_SaveVersion the slot was written with (0 if unreadable). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 SaveVersion = 0;

	/** The current build's FDP_SaveVersion::LatestVersion (for UI display). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 LatestVersion = 0;

	/** True if loading this slot would trigger the migration chain (Status == NeedsMigration). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	bool bNeedsMigration = false;

	/** True if this slot is a wrapped (.dpcsav) container vs a plain core (.dpsav) save. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	bool bWrapped = false;
};

/**
 * Save-versioning UX helper driven ONLY by real readable data: the core ReadSlotHeader's
 * FDP_SaveHeader.SaveVersion compared to FDP_SaveVersion::LatestVersion, plus the wrapped container CRC.
 *
 * It classifies each slot as Current / NeedsMigration / IncompatibleNewer / IncompatibleOlder / Corrupt /
 * Missing so a save-management screen can list incompatible/older saves and flag those that need migration.
 *
 * DRY-RUN: the core UDP_SaveMigration exposes no step-coverage query, so DryRunMigrationCheck is a
 * documented HEURISTIC — it reports "migratable" when the slot's version is older than Latest but no older
 * than the oldest supported version. A true chain-coverage check would require a NEW public const
 * UDP_SaveMigration::CanReach (an additive CORE change, called out, NOT assumed here).
 *
 * Reuses ISeam_SaveSlotManager for slot enumeration (resolved from the locator), falling back to the core
 * subsystem's GetAllSlots when the seam is unavailable.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_VersionInspector : public UObject
{
	GENERATED_BODY()

public:
	/** Classify every known slot (wrapped + plain). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void ClassifyAllSlots(TArray<FSaveX_SlotVersionInfo>& Out) const;

	/** Classify a single slot. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	ESaveX_VersionStatus ClassifySlot(const FString& Slot) const;

	/** Full per-slot info for one slot (label + version + flags). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool GetSlotVersionInfo(const FString& Slot, FSaveX_SlotVersionInfo& Out) const;

	/**
	 * Heuristic dry-run: returns true if loading Slot would run (and is expected to succeed) the migration
	 * chain. OutReason carries a human-readable explanation for UI. NOT a guarantee — see class comment.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DryRunMigrationCheck(const FString& Slot, FString& OutReason) const;

	/** The oldest FDP_SaveVersion the current migration chain is assumed to support (heuristic floor). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save")
	int32 GetOldestSupportedVersion() const;

private:
	/** Resolve the core save subsystem (header reader). */
	UDP_SaveGameSubsystem* GetCoreSaveSubsystem() const;

	/** Resolve the storage subsystem (wrapped header reader). */
	USaveX_StorageSubsystem* GetStorage() const;

	/** Enumerate every known slot name (wrapped + plain), de-duplicated. */
	void EnumerateSlots(TArray<FString>& OutSlots) const;
};
