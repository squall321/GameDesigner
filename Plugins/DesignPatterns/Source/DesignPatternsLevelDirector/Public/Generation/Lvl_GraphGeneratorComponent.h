// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Placement/Lvl_PlacementTypes.h"
#include "Generation/Lvl_GraphTypes.h"
#include "Save/Lvl_GraphSaveTypes.h"
#include "Persist/Seam_Persistable.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Lvl_GraphGeneratorComponent.generated.h"

class ULvl_DungeonGraphRuleSet;
class ULvl_ProceduralPlacerComponent;

/**
 * AUTHORITY-ONLY deterministic dungeon assembly.
 *
 * From a ULvl_DungeonGraphRuleSet and a single FRandomStream(GetEffectiveSeed()) this builds, in order:
 *   1. A room GRAPH — a grid/BSP-style pack of non-overlapping rooms over the rule set's cell grid,
 *      connected by a minimum-spanning corridor tree plus a data-driven fraction of extra LOOP edges.
 *   2. A WFC-style TILE collapse — each carved cell collapses to a tile rule whose connector mask is
 *      consistent with its neighbours (or, when the rule set's TileRules are empty, the cell simply
 *      records its kind — a documented inert fallback to a plain carve).
 *   3. PREFAB STAMPING — collapsed tiles matching a prefab-stamp rule contribute spawnable entries.
 * The result is an FLvl_PlacementManifest fed to a co-located ULvl_ProceduralPlacerComponent through the
 * EXISTING public RestoreFromManifest, so spawning flows through the core factory + pool exactly like
 * the base placer. Determinism: the WHOLE pass is driven by one seeded stream, so the layout is
 * reproducible across runs and across save/load.
 *
 * PERSISTENCE: this component implements ISeam_Persistable ITSELF with its own record kind
 * (DP.Persist.Lvl.Graph), so graphs save independently of ULvl_SaveGame's single Placement-kind
 * contract (which is left untouched). RestoreState is authority-guarded and either replays the saved
 * manifest verbatim or regenerates from the stored seed, per the captured ELvl_RestoreStrategy.
 *
 * REPLICATION: nothing here replicates. Generation is server-authoritative; the spawned actors and any
 * World-Partition state replicate themselves. GenerateGraph / RestoreState guard authority at the TOP.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_GraphGeneratorComponent : public UActorComponent, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	ULvl_GraphGeneratorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** True on server / standalone / listen-server host. ALL generation gates on this at the TOP. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Graph")
	bool HasWorldAuthority() const;

	// ---- Configuration --------------------------------------------------------------------------

	/** The dungeon graph rule set this generator executes. Required for a pass to run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	TObjectPtr<ULvl_DungeonGraphRuleSet> GraphRuleSet;

	/**
	 * The base placer this generator drives. When null, BeginPlay resolves a sibling
	 * ULvl_ProceduralPlacerComponent on the owner (composition, not inheritance).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	TObjectPtr<ULvl_ProceduralPlacerComponent> TargetPlacer;

	/** Optional seed override. >= 0 replaces the rule set's seed for this generator. -1 = use rule policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	int32 SeedOverride = -1;

	/** Optional region tag override (replaces the rule set's DefaultRegionTag in the manifest/record). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag RegionTagOverride;

	/** If true, GenerateGraph runs automatically on BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	bool bGenerateOnBeginPlay = false;

	/** If true, the generated graph is cleared (placer torn down) on EndPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	bool bClearOnEndPlay = true;

	/** Restore strategy captured into the save record (verbatim replay vs regenerate-from-seed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph|Save")
	ELvl_RestoreStrategy RestoreStrategy = ELvl_RestoreStrategy::RestoreManifestVerbatim;

	// ---- Pass control (AUTHORITY ONLY) ----------------------------------------------------------

	/**
	 * Build the dungeon graph + tiles + prefab manifest and feed it to the target placer. Authority-
	 * guarded at the top; gated on the rule set's GateKey (default open when the gate seam is unresolved).
	 * @return true if the pass placed at least one actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Graph")
	bool GenerateGraph();

	/** The manifest of the current graph (empty before GenerateGraph). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Graph")
	const FLvl_PlacementManifest& GetGraphManifest() const { return GraphManifest; }

	/** Effective seed (override if >= 0, else the rule set's seed policy resolved to a concrete int). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Graph")
	int32 GetEffectiveSeed() const;

	/** Effective region tag (override if set, else the rule set's DefaultRegionTag). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag GetEffectiveRegionTag() const;

	// ---- Generation stages (public for testing/tooling; all const, deterministic) ---------------

	/** Pack rooms over the rule set's cell grid and connect them (spanning tree + loops). */
	void BuildRoomGraph(FRandomStream& Stream, TArray<FLvl_RoomNode>& OutRooms,
		TArray<FLvl_CorridorEdge>& OutEdges) const;

	/** Carve rooms + corridors into tile cells and collapse them WFC-style. Returns false on failure. */
	bool CollapseTiles(FRandomStream& Stream, const TArray<FLvl_RoomNode>& Rooms,
		const TArray<FLvl_CorridorEdge>& Edges, TArray<FLvl_TileCell>& OutCells) const;

	/** Convert collapsed tiles into manifest entries via the prefab-stamp table. */
	void StampPrefabs(const TArray<FLvl_TileCell>& Cells, FRandomStream& Stream,
		FLvl_PlacementManifest& OutManifest) const;

	//~ Begin ISeam_Persistable
	/** Pack the current graph (seed + rule-set tag + manifest + strategy) into an FInstancedStruct. */
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	/** Authority-guarded restore: replay verbatim or regenerate from seed per the stored strategy. */
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	/** DP.Persist.Lvl.Graph — distinct from the placer's Placement kind. */
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

private:
	/** The manifest produced by the current pass. NOT replicated; rebuilt authority-side and saved. */
	UPROPERTY(Transient)
	FLvl_PlacementManifest GraphManifest;

	/** Resolve the target placer (explicit, else a sibling on the owner). May be null. */
	ULvl_ProceduralPlacerComponent* ResolveTargetPlacer() const;

	/** Broadcast DP.Bus.Lvl.Placement.Generated for the graph pass (reuses the placement channel). */
	void BroadcastGraphGenerated(int32 PlacedCount) const;

	/** Cell-grid neighbour offsets for the four cardinal directions (matches ELvl_TileConnector order). */
	static const FIntPoint& CardinalOffset(int32 Direction);
};
