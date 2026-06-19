// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DPSaveMigration.generated.h"

class UDP_SaveGame;

/**
 * A single, ordered up-conversion step in the save migration chain.
 *
 * Each step declares the version it migrates FROM and TO (To is normally From+1). The
 * registry walks the chain From -> ... -> Latest, applying steps in order so a save written
 * many versions ago is brought fully current. Subclass and override Apply() with the field
 * fix-up for that one version bump; keep each step small and idempotent.
 */
UCLASS(Abstract, BlueprintType, Blueprintable, EditInlineNew)
class DESIGNPATTERNS_API UDP_SaveMigrationStep : public UObject
{
	GENERATED_BODY()

public:
	/** The FDP_SaveVersion::Type this step upgrades from. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Save")
	int32 GetFromVersion() const;
	virtual int32 GetFromVersion_Implementation() const { return 0; }

	/** The FDP_SaveVersion::Type this step produces (usually GetFromVersion()+1). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Save")
	int32 GetToVersion() const;
	virtual int32 GetToVersion_Implementation() const { return 0; }

	/** Apply the up-conversion to Save in-place. Return true on success. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Save")
	bool Apply(UDP_SaveGame* Save) const;
	virtual bool Apply_Implementation(UDP_SaveGame* Save) const { return true; }
};

/**
 * Ordered registry of migration steps that drives a save object from its on-disk version up
 * to FDP_SaveVersion::LatestVersion.
 *
 * Held (and owned, as a UPROPERTY array) by the save subsystem. Register steps once at
 * startup; Migrate() then chains the relevant steps for any given starting version. The
 * subsystem also calls the save object's own virtual Migrate() hook for type-specific logic.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNS_API UDP_SaveMigration : public UObject
{
	GENERATED_BODY()

public:
	/** Register a migration step instance. Steps are sorted by FromVersion on registration. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	void RegisterStep(UDP_SaveMigrationStep* Step);

	/** Construct, register and return a step of the given class (convenience). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save", meta = (DeterminesOutputType = "StepClass"))
	UDP_SaveMigrationStep* RegisterStepClass(TSubclassOf<UDP_SaveMigrationStep> StepClass);

	/**
	 * Apply every registered step whose FromVersion is >= FromVersion, in ascending order,
	 * mutating Save in-place. Returns true if the chain reached ToVersion (or no steps were
	 * needed). Logs and returns false if a step fails or a version gap can't be bridged.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool Migrate(UDP_SaveGame* Save, int32 FromVersion, int32 ToVersion) const;

	/** Number of registered steps. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	int32 NumSteps() const { return Steps.Num(); }

private:
	/** Registered steps, kept sorted by FromVersion ascending. Owned: instanced subobjects. */
	UPROPERTY()
	TArray<TObjectPtr<UDP_SaveMigrationStep>> Steps;
};
