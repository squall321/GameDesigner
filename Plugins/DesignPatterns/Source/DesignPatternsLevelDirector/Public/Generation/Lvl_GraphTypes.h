// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Lvl_GraphTypes.generated.h"

/**
 * One node in a procedural dungeon ROOM GRAPH.
 *
 * Produced deterministically by ULvl_GraphGeneratorComponent::BuildRoomGraph from a single seeded
 * FRandomStream. Pure value type (no UObject refs, no soft pointers) so the whole graph can be folded
 * into an FLvl_PlacementManifest / save record and replayed identically. Cells are integer grid
 * coordinates on the rule set's GridDimensions; the generator converts cell space to world space using
 * the rule set's CellWorldSize and the owning actor transform.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_RoomNode
{
	GENERATED_BODY()

	/** Stable index of this room within the graph (also its node id for edges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	int32 NodeId = INDEX_NONE;

	/** Bottom-left corner of the room in cell coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	FIntPoint OriginCell = FIntPoint::ZeroValue;

	/** Room size in cells (width, height). Always >= 1 in each axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	FIntPoint SizeCells = FIntPoint(1, 1);

	/**
	 * Optional designer category for the room (e.g. DP.Lvl.Room.Combat / .Treasure / .Boss). Used by
	 * the prefab-stamp table to pick a matching stamp. Empty -> any stamp tagged for "any room".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag RoomCategory;

	FLvl_RoomNode() = default;

	/** Centre of the room in cell space (rounded down — deterministic). */
	FIntPoint GetCentreCell() const
	{
		return FIntPoint(OriginCell.X + SizeCells.X / 2, OriginCell.Y + SizeCells.Y / 2);
	}

	/** True if a cell lies inside this room's footprint. */
	bool ContainsCell(const FIntPoint& Cell) const
	{
		return Cell.X >= OriginCell.X && Cell.X < OriginCell.X + SizeCells.X
			&& Cell.Y >= OriginCell.Y && Cell.Y < OriginCell.Y + SizeCells.Y;
	}
};

/**
 * One connection between two rooms in the graph (a corridor to carve). Stored as node-id endpoints so
 * the edge survives reordering and is trivially serializable.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_CorridorEdge
{
	GENERATED_BODY()

	/** Source room node id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	int32 FromNode = INDEX_NONE;

	/** Destination room node id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	int32 ToNode = INDEX_NONE;

	/** True if this edge is an EXTRA loop (added beyond the spanning tree) rather than a tree edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	bool bIsLoop = false;

	FLvl_CorridorEdge() = default;

	FLvl_CorridorEdge(int32 InFrom, int32 InTo, bool bInLoop = false)
		: FromNode(InFrom), ToNode(InTo), bIsLoop(bInLoop) {}
};

/** What a single collapsed tile cell represents after WFC-style assembly. */
UENUM(BlueprintType)
enum class ELvl_TileKind : uint8
{
	/** Solid rock / unused — nothing stamped here. */
	Empty,

	/** Inside a room footprint. */
	Room,

	/** Carved corridor connecting rooms. */
	Corridor,

	/** A doorway cell where a corridor meets a room edge (good place for a door prefab). */
	Door
};

/**
 * One collapsed cell of the dungeon grid after WFC-style tile assembly. Carries the chosen tile rule
 * tag (from the rule set's TileRules) so prefab stamping can pick a visual/blocking prefab per tile.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_TileCell
{
	GENERATED_BODY()

	/** Cell coordinate on the dungeon grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	FIntPoint Cell = FIntPoint::ZeroValue;

	/** What this cell is (room / corridor / door / empty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	ELvl_TileKind Kind = ELvl_TileKind::Empty;

	/** The WFC tile-rule tag the cell collapsed to (matched against the rule set's TileRules). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag TileTag;

	/** Room node id this cell belongs to (INDEX_NONE for corridors / empty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Lvl|Graph")
	int32 OwningRoom = INDEX_NONE;

	FLvl_TileCell() = default;

	FLvl_TileCell(const FIntPoint& InCell, ELvl_TileKind InKind)
		: Cell(InCell), Kind(InKind) {}

	bool IsCarved() const { return Kind != ELvl_TileKind::Empty; }
};

/**
 * Bit flags for the four cardinal connector sides of a WFC tile / prefab. Used to constrain which
 * tiles may sit adjacent (a tile's east connector must match its neighbour's west connector).
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ELvl_TileConnector : uint8
{
	None  = 0      UMETA(Hidden),
	North = 1 << 0,
	East  = 1 << 1,
	South = 1 << 2,
	West  = 1 << 3
};
ENUM_CLASS_FLAGS(ELvl_TileConnector);

/**
 * One WFC-style tile rule: a tile tag, the connector sides it exposes, a relative weight, and the
 * room categories it is allowed in. Authored on ULvl_DungeonGraphRuleSet. No behaviour.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_WfcTileRule
{
	GENERATED_BODY()

	/** Stable id of this tile (also recorded into the collapsed FLvl_TileCell.TileTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag TileTag;

	/** Which sides of this tile expose an open connector (bitmask of ELvl_TileConnector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph",
		meta = (Bitmask, BitmaskEnum = "/Script/DesignPatternsLevelDirector.ELvl_TileConnector"))
	uint8 ConnectorMask = 0;

	/** Tile kinds this rule may collapse onto (empty -> any kind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	TArray<ELvl_TileKind> AllowedKinds;

	/** Relative selection weight (>= 0; all-zero in a candidate set -> uniform). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	FLvl_WfcTileRule() = default;

	/** True if this rule may be placed on a cell of the given kind. */
	bool AllowsKind(ELvl_TileKind Kind) const
	{
		return AllowedKinds.Num() == 0 || AllowedKinds.Contains(Kind);
	}
};

/**
 * One prefab-stamp rule: when a room/corridor matches the predicate, stamp the actor identified by
 * ActorClassTag (resolved through the core spawn factory, exactly like FLvl_PlacementClassChoice) at
 * the rule's anchor. Storing a tag (not a class pointer) keeps the produced manifest save-safe.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PrefabStampRule
{
	GENERATED_BODY()

	/** Stable spawn identity for the stamped actor (factory lookup + saved manifest entry). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag ActorClassTag;

	/**
	 * If valid, this stamp only applies to tiles whose TileTag matches (is a child of) this. Empty ->
	 * applies to any tile of the kinds below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	FGameplayTag RequiredTileTag;

	/** Tile kinds this stamp applies to (empty -> any kind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	TArray<ELvl_TileKind> AppliesToKinds;

	/** Chance in [0,1] that an eligible tile actually receives this stamp (deterministic via stream). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StampChance = 1.0f;

	/** Local vertical offset (cm) applied to the stamp anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	float VerticalOffset = 0.0f;

	/** If true, the stamp is yaw-aligned to face the room/corridor centre (deterministic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Graph")
	bool bFaceInteriorCentre = false;

	FLvl_PrefabStampRule() = default;

	bool IsUsable() const { return ActorClassTag.IsValid(); }

	/** True if a cell of the given kind/tile satisfies this rule's predicate. */
	bool Matches(ELvl_TileKind Kind, const FGameplayTag& TileTag) const
	{
		if (AppliesToKinds.Num() > 0 && !AppliesToKinds.Contains(Kind))
		{
			return false;
		}
		if (RequiredTileTag.IsValid())
		{
			return TileTag.IsValid() && TileTag.MatchesTag(RequiredTileTag);
		}
		return true;
	}
};
