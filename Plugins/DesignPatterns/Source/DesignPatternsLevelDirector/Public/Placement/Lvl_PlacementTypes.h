// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Lvl_PlacementTypes.generated.h"

/**
 * One actor placed by the procedural placer.
 *
 * This is the SAVE-FACING record of a single scatter result. It is intentionally minimal and
 * fully value-typed (no UObject refs to GC, no soft pointers to resolve eagerly) so a whole
 * manifest can be serialized into a UDP_SaveGame and replayed deterministically on restore.
 *
 * The actor to spawn is identified by ActorClassTag — a stable design-time GameplayTag that maps
 * (through the core spawn factory's registry, or a rule set's class table) to a concrete actor
 * class. Storing a TAG rather than a hard/soft class pointer keeps the manifest decoupled from
 * the exact asset and survives asset renames, and keeps the save blob path-free.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PlacedEntry
{
	GENERATED_BODY()

	/**
	 * Stable identity of the placed actor's class. Resolved at spawn time through the core spawn
	 * factory (UDP_SpawnFactorySubsystem::IsFactoryRegistered / Spawn) or the rule set's soft-class
	 * table. Never a raw object path, so the manifest is rename-safe and replication-free.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag ActorClassTag;

	/** Final world transform the actor was (and will be) placed at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FTransform Transform = FTransform::Identity;

	/**
	 * Stable per-placement identity. Generated deterministically from the rule set's seed and the
	 * candidate index so the SAME entry gets the SAME id every run — this lets a restore reconcile
	 * "already-spawned" entries and lets gameplay address an individual placed instance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FGuid PlacementId;

	FLvl_PlacedEntry() = default;

	FLvl_PlacedEntry(const FGameplayTag& InClassTag, const FTransform& InTransform, const FGuid& InId)
		: ActorClassTag(InClassTag)
		, Transform(InTransform)
		, PlacementId(InId)
	{
	}

	/** A placement is usable only if it names a class and has a stable id. */
	bool IsValid() const { return ActorClassTag.IsValid() && PlacementId.IsValid(); }
};

/**
 * A complete record of one procedural placement pass: the rule set that produced it, the seed that
 * made it deterministic, and every placed entry.
 *
 * Persisted via a UDP_SaveGame (see ULvl_SaveGame) so a save/load reproduces the exact same world
 * dressing. The pass is the unit of save/restore: clearing and regenerating replaces the whole
 * manifest. Because RandomSeed and the rule-set identity are stored alongside the entries, a
 * restore can either (a) re-spawn the stored entries verbatim, or (b) re-run the generator from
 * the same seed and arrive at the same result — both are deterministic.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PlacementManifest
{
	GENERATED_BODY()

	/**
	 * The DataTag of the ULvl_PlacementRuleSet that produced this pass. Lets a restore find the rule
	 * set again (via the data registry) to re-derive class tables / spacing if it chooses to
	 * regenerate rather than replay verbatim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag RuleSetTag;

	/**
	 * A logical identity for the placement region/owner this manifest belongs to (e.g. a level-tile
	 * tag or a streaming-cell tag). One world may host several independent placement passes; this
	 * disambiguates them in the save so restores route back to the correct placer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag RegionTag;

	/** The seed the pass was generated with. Stored so the pass is reproducible from data alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	int32 RandomSeed = 0;

	/** Every actor placed by the pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	TArray<FLvl_PlacedEntry> Entries;

	FLvl_PlacementManifest() = default;

	/** Drop all entries (keeps the rule-set/region/seed identity). */
	void Reset()
	{
		Entries.Reset();
	}

	/** True if this manifest carries at least one valid entry. */
	bool HasEntries() const { return Entries.Num() > 0; }

	int32 Num() const { return Entries.Num(); }
};

/**
 * The FInstancedStruct record kind a ULvl_ProceduralPlacerComponent contributes through
 * ISeam_Persistable::CaptureState / RestoreState. Wrapping the manifest in a dedicated record
 * struct (rather than persisting the manifest directly) keeps the persistence seam's payload type
 * self-describing and lets the kind tag route records back to the right participant.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PlacementSaveRecord
{
	GENERATED_BODY()

	/** The captured manifest for one placer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Placement")
	FLvl_PlacementManifest Manifest;

	FLvl_PlacementSaveRecord() = default;

	explicit FLvl_PlacementSaveRecord(const FLvl_PlacementManifest& InManifest)
		: Manifest(InManifest)
	{
	}
};
