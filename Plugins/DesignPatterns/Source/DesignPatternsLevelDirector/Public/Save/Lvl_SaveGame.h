// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"
#include "Placement/Lvl_PlacementTypes.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Lvl_SaveGame.generated.h"

class ULvl_ProceduralPlacerComponent;

/**
 * Save object for the LevelDirector placement area.
 *
 * Stores one FLvl_PlacementManifest per procedural placer in the world and, on restore, re-spawns
 * each manifest deterministically through the authority-guarded placer path. A CLIENT-side load is a
 * NO-OP: the placers' RestoreFromManifest guard on authority, and clients receive the re-spawned
 * actors through normal replication.
 *
 * This object also wraps a single placer's manifest as an ISeam_Persistable record so it can take
 * part in the universal save-participant flow (the kind tag DP.Persist.Lvl.Placement routes records
 * back). CaptureState/RestoreState here operate on THIS object's combined manifest list; an
 * individual placer implementing the seam would carry just its own manifest (see the component path).
 *
 * Pak mounts and streaming state are deliberately NOT persisted here — those are per-machine
 * environment state, re-established from settings on load, never replayed from a save (a save that
 * re-mounts paks would be a security and portability hazard).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_SaveGame : public UDP_SaveGame, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	/**
	 * Every captured placement manifest, one per placer. SaveGame-tagged so the standard save
	 * serialization writes them into the versioned blob.
	 */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Save")
	TArray<FLvl_PlacementManifest> Manifests;

	/**
	 * Gather every procedural placer's manifest from WorldContext's world into Manifests. Call on the
	 * game thread before saving (the DP save subsystem's OnPreSave path can drive this). Authority is
	 * not required to CAPTURE (reading the authority-side manifest is harmless), but a manifest is only
	 * meaningfully populated on the machine that generated it.
	 * @return number of manifests captured.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Save", meta = (WorldContext = "WorldContextObject"))
	int32 CaptureFromWorld(const UObject* WorldContextObject);

	/**
	 * Re-spawn every stored manifest into WorldContext's world via the authority-guarded placer path.
	 * CLIENT-SIDE: a no-op (the placers reject non-authority restore; clients get the actors via
	 * replication). @return number of actors re-spawned across all placers.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Save", meta = (WorldContext = "WorldContextObject"))
	int32 RestoreInto(const UObject* WorldContextObject);

	//~ Begin UDP_SaveGame
	/** OnPreSave is invoked on the game thread by the DP save subsystem; capture happens via CaptureFromWorld. */
	virtual void OnPreSave_Implementation() override;
	/** OnPostLoad scatters back by calling RestoreInto against the active world. */
	virtual void OnPostLoad_Implementation() override;
	//~ End UDP_SaveGame

	//~ Begin ISeam_Persistable
	/** Pack the combined manifest list into a single FInstancedStruct record. */
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	/** Unpack a previously-captured record into Manifests (does NOT spawn — call RestoreInto for that). */
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	/** The persistence kind tag for placement records (DP.Persist.Lvl.Placement). */
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	/** Set the world used by the parameterless OnPreSave/OnPostLoad hooks (the save subsystem sets this). */
	void SetSaveWorldContext(const UObject* InWorldContextObject)
	{
		SaveWorldContext = const_cast<UObject*>(InWorldContextObject);
	}

private:
	/**
	 * World-context object used by the parameterless save/load hooks. Weak + non-owning: the save
	 * object must never keep a world alive. Null-checked before use. Not reflected — it is transient
	 * wiring set by the save subsystem, never serialized.
	 */
	TWeakObjectPtr<UObject> SaveWorldContext;

	/** Find every procedural placer in the given world (used by capture/restore). */
	static void CollectPlacers(const UObject* WorldContextObject, TArray<ULvl_ProceduralPlacerComponent*>& OutPlacers);
};
