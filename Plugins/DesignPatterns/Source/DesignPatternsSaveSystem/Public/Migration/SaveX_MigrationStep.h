// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveMigration.h"
#include "Save/DPSaveVersion.h"
#include "SaveX_MigrationStep.generated.h"

class UDP_SaveGame;

/**
 * SaveSystem migration step that bridges the FDP_SaveVersion bump where per-save user metadata
 * (DisplayName / PlaytimeSeconds) was introduced — FDP_SaveVersion::InitialVersion ->
 * FDP_SaveVersion::AddedSlotMetadata.
 *
 * Concretely: a blob written at InitialVersion has no DisplayName, so after the core loader has
 * parsed the body this step backfills a sensible non-empty DisplayName (and leaves PlaytimeSeconds
 * at its deserialized default of 0) so the slot UI and ListSlots never show a blank label for an
 * old save. The step is idempotent (it only fills a DisplayName that is still empty).
 *
 * The slot manager's Initialize() registers this step via the CORE subsystem's
 * UDP_SaveMigration::RegisterStepClass — NOT at module startup, because there is no GameInstance
 * (and therefore no core save subsystem) during module load.
 *
 * To add a future version bump, ship another USaveX_MigrationStep subclass (or a sibling step
 * class) keyed to the new From/To pair and register it the same way; the core registry chains
 * steps in ascending order so a very old save is brought fully current.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, meta = (DisplayName = "SaveX Migration: Add Slot Metadata"))
class DESIGNPATTERNSSAVESYSTEM_API USaveX_MigrationStep : public UDP_SaveMigrationStep
{
	GENERATED_BODY()

public:
	USaveX_MigrationStep();

	//~ Begin UDP_SaveMigrationStep
	/** Upgrades from FDP_SaveVersion::InitialVersion. */
	virtual int32 GetFromVersion_Implementation() const override;

	/** Produces FDP_SaveVersion::AddedSlotMetadata. */
	virtual int32 GetToVersion_Implementation() const override;

	/** Backfill a non-empty DisplayName on pre-metadata saves. Returns true (idempotent, never fails). */
	virtual bool Apply_Implementation(UDP_SaveGame* Save) const override;
	//~ End UDP_SaveMigrationStep

	/**
	 * Fallback DisplayName assigned to a migrated save that had none. EditAnywhere so it is not a
	 * hard-coded magic string; the slot manager substitutes the slot name at gather time when it can,
	 * but a step that runs without slot context uses this label.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save")
	FString BackfilledDisplayName = TEXT("Recovered Save");
};
