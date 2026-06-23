// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Placement/Lvl_PlacementTypes.h"
#include "Lvl_GraphSaveTypes.generated.h"

/**
 * How a dungeon graph is reconstructed on restore. Chosen by the designer on
 * ULvl_GraphGeneratorComponent and stored in the save record so a load reproduces the SAME world.
 */
UENUM(BlueprintType)
enum class ELvl_RestoreStrategy : uint8
{
	/**
	 * Re-spawn the saved manifest verbatim (replay every stored FLvl_PlacedEntry by tag+transform).
	 * Most robust: independent of the generator's algorithm version, so saves survive code changes.
	 */
	RestoreManifestVerbatim,

	/**
	 * Re-run the generator from the stored RandomSeed. Smaller save footprint and self-healing if the
	 * content/prefab tables changed, but only reproduces the original layout when the generation
	 * algorithm is unchanged. The generator falls back to verbatim if regeneration produces nothing.
	 */
	RegenerateFromSeed
};

/**
 * The FInstancedStruct record kind (DP.Persist.Lvl.Graph) that ULvl_GraphGeneratorComponent
 * contributes through its OWN ISeam_Persistable implementation — deliberately SEPARATE from the
 * placer's FLvl_PlacementSaveRecord (DP.Persist.Lvl.Placement) so dungeon graphs persist independently
 * and ULvl_SaveGame's single Placement-kind contract is never touched.
 *
 * Pure value type (no UObject refs): the seed + rule-set tag let a RegenerateFromSeed restore re-run
 * the generator; the embedded manifest lets a RestoreManifestVerbatim restore replay exactly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_GraphSaveRecord
{
	GENERATED_BODY()

	/** DataTag of the ULvl_DungeonGraphRuleSet that produced the graph (for a regenerate restore). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph|Save")
	FGameplayTag GraphRuleSetTag;

	/** Logical region/owner the graph belongs to (routes the record back to the right generator). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph|Save")
	FGameplayTag RegionTag;

	/** The deterministic seed the graph was generated with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph|Save")
	int32 RandomSeed = 0;

	/** The produced placement manifest (replayed verbatim, or rebuilt when regenerating). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph|Save")
	FLvl_PlacementManifest Manifest;

	/** The restore strategy the generator captured (verbatim vs regenerate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph|Save")
	ELvl_RestoreStrategy RestoreStrategy = ELvl_RestoreStrategy::RestoreManifestVerbatim;

	FLvl_GraphSaveRecord() = default;
};
