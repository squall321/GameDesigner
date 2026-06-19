// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Save/DPSaveVersion.h"
#include "DPSaveGame.generated.h"

/**
 * Base class for DesignPatterns save objects.
 *
 * Subclass and add UPROPERTY()s — the standard SaveGame UPROPERTY tagging serializes them.
 * The DP save subsystem wraps this body in a versioned, header-prefixed blob; on load it
 * calls Migrate() so an out-of-date subclass can up-convert fields in-place before the game
 * reads them.
 *
 * Override OnPreSave/OnPostLoad for gather/scatter of live game state, and Migrate() for
 * version-specific fix-ups. Keep these deterministic and main-thread-only — the subsystem
 * invokes them on the game thread.
 */
UCLASS(BlueprintType, Blueprintable)
class DESIGNPATTERNS_API UDP_SaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Designer/player-facing label for this slot; copied into the save header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save")
	FString DisplayName;

	/** Accumulated in-game playtime in seconds; copied into the save header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save")
	float PlaytimeSeconds = 0.f;

	/**
	 * The FDP_SaveVersion the loaded blob was written with. Set by the subsystem during load
	 * BEFORE Migrate() runs so migration steps can branch on it. Equals LatestVersion on a
	 * freshly-constructed (unsaved) object.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save")
	int32 LoadedFromVersion = static_cast<int32>(FDP_SaveVersion::LatestVersion);

	/**
	 * Gather hook: called on the game thread immediately before the object is serialized to
	 * bytes. Pull live world state into UPROPERTYs here.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Save")
	void OnPreSave();
	virtual void OnPreSave_Implementation() {}

	/**
	 * Scatter hook: called on the game thread after load + migration completes. Push restored
	 * state back into the live world here.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Save")
	void OnPostLoad();
	virtual void OnPostLoad_Implementation() {}

	/**
	 * Migration hook. Called after the body is deserialized when LoadedFromVersion is older
	 * than FDP_SaveVersion::LatestVersion. Up-convert fields here; the subsystem will also run
	 * any registered UDP_SaveMigration steps. Return true on success.
	 *
	 * @param FromVersion  The FDP_SaveVersion::Type the blob was written with.
	 * @param ToVersion    The current FDP_SaveVersion::LatestVersion target.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Save")
	bool Migrate(int32 FromVersion, int32 ToVersion);
	virtual bool Migrate_Implementation(int32 FromVersion, int32 ToVersion) { return true; }
};
