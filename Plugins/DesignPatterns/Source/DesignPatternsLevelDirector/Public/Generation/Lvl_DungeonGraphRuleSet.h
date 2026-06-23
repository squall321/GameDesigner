// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Generation/Lvl_GraphTypes.h"
#include "Lvl_DungeonGraphRuleSet.generated.h"

/**
 * Designer-authored, DATA-ONLY rule set describing HOW to assemble a procedural dungeon:
 *   - a room graph (BSP/grid layout over a cell grid, with a min-spanning corridor tree + extra loops),
 *   - a WFC-style tile-adjacency table (TileRules + connector masks) for corridor/room cell tiling,
 *   - a prefab-stamp table (PrefabStamps) that converts collapsed tiles into spawnable actor entries,
 *   - and a deterministic RandomSeed.
 *
 * Mirrors ULvl_PlacementRuleSet exactly: EVERY tunable is an EditAnywhere property with ClampMin meta
 * (no magic numbers reach the generator), and IsDataValid flags an empty/degenerate configuration. The
 * asset has NO runtime behaviour — ULvl_GraphGeneratorComponent reads it and runs the deterministic
 * assembly, ultimately producing an FLvl_PlacementManifest spawned through the existing placer path.
 *
 * Determinism policy matches the placer: RandomSeed 0 means "derive from the owning actor's stable
 * name hash" (reproducible-but-unique per generator); non-zero is an exact shared seed.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_DungeonGraphRuleSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	ULvl_DungeonGraphRuleSet();

	// ---- Identity / region ----------------------------------------------------------------------

	/** Logical region/owner this graph belongs to; copied into the produced manifest's RegionTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Identity")
	FGameplayTag DefaultRegionTag;

	// ---- Determinism ----------------------------------------------------------------------------

	/** Seed for the deterministic FRandomStream. 0 -> derive from owner name hash (same policy as the placer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Determinism")
	int32 RandomSeed = 0;

	// ---- Grid -----------------------------------------------------------------------------------

	/** Dungeon grid size in cells. Rooms/corridors are laid out inside this. Each axis clamped >= 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Grid",
		meta = (ClampMin = "1"))
	FIntPoint GridDimensions = FIntPoint(24, 24);

	/** World size (cm) of one grid cell, used to convert cell space to the world transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Grid",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float CellWorldSize = 400.0f;

	// ---- Rooms ----------------------------------------------------------------------------------

	/** Number of rooms to attempt to place (clamped >= 1 at use). The packer rejects overlaps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Rooms",
		meta = (ClampMin = "1", UIMin = "1"))
	FInt32Interval RoomCountRange = FInt32Interval(6, 10);

	/** Min/max room size in cells (X). Clamped >= 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Rooms",
		meta = (ClampMin = "1.0"))
	FVector2D RoomWidthRangeCells = FVector2D(3.0, 6.0);

	/** Min/max room size in cells (Y). Clamped >= 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Rooms",
		meta = (ClampMin = "1.0"))
	FVector2D RoomHeightRangeCells = FVector2D(3.0, 6.0);

	/** Max placement attempts per room before giving up (prevents an unbounded packing loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Rooms",
		meta = (ClampMin = "1"))
	int32 RoomPlacementAttempts = 24;

	/** Optional weighted set of room categories assigned round-robin/weighted to placed rooms. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Rooms")
	TArray<FGameplayTag> RoomCategories;

	// ---- Corridors ------------------------------------------------------------------------------

	/**
	 * Fraction (0..1) of EXTRA loop edges added on top of the spanning tree, to give the dungeon cycles
	 * rather than a pure tree. 0 -> tree only; 1 -> add as many loop edges as tree edges (capped).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Corridors",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExtraLoopFraction = 0.15f;

	/** Corridor half-width in cells (0 -> 1-cell-wide corridors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Corridors",
		meta = (ClampMin = "0"))
	int32 CorridorHalfWidthCells = 0;

	// ---- WFC tile table -------------------------------------------------------------------------

	/**
	 * WFC-style tile rules. When non-empty, each carved cell collapses to one of these (constrained by
	 * connector adjacency + allowed kinds). When EMPTY, cells simply record their kind with no TileTag
	 * (the generator degrades to a plain room/corridor carve — a documented inert fallback).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|WFC")
	TArray<FLvl_WfcTileRule> TileRules;

	/** Max WFC propagation iterations (safety cap against a pathological constraint loop). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|WFC",
		meta = (ClampMin = "1"))
	int32 MaxCollapseIterations = 4096;

	// ---- Prefab stamps --------------------------------------------------------------------------

	/** Prefab-stamp rules converting collapsed tiles into spawnable manifest entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Stamps")
	TArray<FLvl_PrefabStampRule> PrefabStamps;

	/** Hard upper bound on stamped actors per pass (safety cap against a huge grid). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Stamps",
		meta = (ClampMin = "0"))
	int32 MaxStamps = 1024;

	// ---- Gate -----------------------------------------------------------------------------------

	/** Gate key the whole generation is gated on (ISeam_ActivationGate; default open when unresolved). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Graph|Gate")
	FGameplayTag GateKey;

	// ---- Derived helpers ------------------------------------------------------------------------

	/**
	 * Pick a weighted WFC tile rule that allows Kind and (when AllowedSet is non-empty) whose TileTag is
	 * in AllowedSet, using the supplied deterministic stream. Returns nullptr if no candidate matches.
	 */
	const FLvl_WfcTileRule* PickTile(FRandomStream& Stream, ELvl_TileKind Kind,
		const FGameplayTagContainer& AllowedSet) const;

	/** Effective room count for this pass, clamped to a sane minimum. */
	int32 GetEffectiveRoomCount(FRandomStream& Stream) const;

	/** Effective room size (cells) sampled from the width/height ranges, clamped >= 1. */
	FIntPoint SampleRoomSize(FRandomStream& Stream) const;

	/** Convert a cell coordinate to a LOCAL-space position (centre of the cell), pre-actor-transform. */
	FVector CellToLocal(const FIntPoint& Cell) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
