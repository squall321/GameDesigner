// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Placement/SimGrid_PlacementTypes.h"
#include "SimGrid_PlacementRuleStrategy.generated.h"

/**
 * Strategy-pattern base for a single placement rule. Each rule is an EditInlineNew, Instanced UObject
 * so designers compose a placement policy by stacking rules on a USimGrid_PlacementComponent (or a
 * placeable's data asset) entirely in the editor. Evaluate is a pure, const, client-safe predicate
 * over an FSimGrid_PlacementContext — it queries the grid only through the read seam and NEVER mutates
 * state — so the exact same rules run for the client ghost preview and for the server's authoritative
 * re-check.
 *
 * Rules return a tri-state FSimGrid_RuleResult: a rule that cannot decide because the client lacks
 * replicated grid data must return Unknown (not Invalid), so the aggregate stays conservative and the
 * server re-evaluates authoritatively.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, Blueprintable, CollapseCategories)
class DESIGNPATTERNSSIMGRID_API USimGrid_PlacementRuleStrategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Judge the candidate placement described by Context. Pure and const; must not mutate any state.
	 * Default implementation passes (returns Valid) so an unconfigured custom rule is non-blocking.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Placement")
	FSimGrid_RuleResult Evaluate(const FSimGrid_PlacementContext& Context) const;
	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const;
};

/**
 * Every footprint cell (after rotation) must be a valid cell within the grid bounds. Always
 * authoritatively decidable on any machine (bounds are config, not replicated state), so it never
 * returns Unknown.
 */
UCLASS(meta = (DisplayName = "In Bounds"))
class DESIGNPATTERNSSIMGRID_API USimGrid_Rule_InBounds : public USimGrid_PlacementRuleStrategy
{
	GENERATED_BODY()

public:
	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const override;
};

/**
 * Every footprint cell must currently be empty. On a client this hits the tri-state snapshot: a cell
 * whose state is Unknown yields an Unknown rule result (the server will re-check), a Set cell yields
 * Invalid, and an Empty cell passes.
 */
UCLASS(meta = (DisplayName = "Cells Empty"))
class DESIGNPATTERNSSIMGRID_API USimGrid_Rule_CellsEmpty : public USimGrid_PlacementRuleStrategy
{
	GENERATED_BODY()

public:
	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const override;
};

/**
 * Terrain gate: each footprint cell's tile-type tag must satisfy the configured Allowed list (if
 * non-empty) and must NOT match the Blocked list. The per-cell FSimGrid_FootprintCell::RequiredTerrain
 * is also honoured (intersected with Allowed). Operates on the cell's tri-state snapshot, returning
 * Unknown for cells the client hasn't received.
 */
UCLASS(meta = (DisplayName = "Terrain Allowed"))
class DESIGNPATTERNSSIMGRID_API USimGrid_Rule_TerrainAllowed : public USimGrid_PlacementRuleStrategy
{
	GENERATED_BODY()

public:
	/**
	 * Tile-type tags the cell's terrain is permitted to be. Empty = no allow-list constraint from this
	 * rule (only the Blocked list and per-cell requirements apply). Matched with tag-hierarchy rules so
	 * a parent tag admits its children.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Terrain")
	FGameplayTagContainer AllowedTerrain;

	/**
	 * Tile-type tags that are never permitted, even if otherwise allowed. Checked with tag-hierarchy
	 * matching. Takes precedence over AllowedTerrain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Terrain")
	FGameplayTagContainer BlockedTerrain;

	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const override;
};

/**
 * Requires that at least RequiredCount of the cells immediately adjacent (4- or 8-neighbourhood) to
 * the footprint carry one of RequiredAdjacentTiles. Models "must be built next to a road / wall /
 * power source". Adjacency is computed against the footprint perimeter, excluding the footprint's own
 * cells. Returns Unknown only if it cannot reach the required count AND some neighbour cells are
 * Unknown (so the server might still satisfy it).
 */
UCLASS(meta = (DisplayName = "Require Adjacent"))
class DESIGNPATTERNSSIMGRID_API USimGrid_Rule_RequireAdjacent : public USimGrid_PlacementRuleStrategy
{
	GENERATED_BODY()

public:
	/** Tile-type tags that count as a satisfying neighbour (hierarchy-matched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Adjacency")
	FGameplayTagContainer RequiredAdjacentTiles;

	/** How many distinct satisfying neighbour cells are required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Adjacency", meta = (ClampMin = "1"))
	int32 RequiredCount = 1;

	/** When true, the diagonal neighbours are considered in addition to the four orthogonal ones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Adjacency")
	bool bIncludeDiagonals = false;

	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const override;
};

/**
 * Requires that every footprint cell is owned (in the territory carrier's sense) by the placement's
 * OwnerId. Models "you can only build in your own zone". This rule queries ownership through the read
 * seam's tile-type channel by convention is NOT possible (ownership is a separate channel), so the
 * rule instead asks the territory carrier resolved off the context's grid provider actor. Because that
 * lookup is best-effort on a client (ownership replicates separately), an unresolved owner yields
 * Unknown rather than Invalid.
 *
 * NOTE: the rule never hard-includes the carrier; it resolves ownership through the grid-provider
 * actor's ISimGrid_GridObserver-style ownership query exposed on the territory facade interface, kept
 * here as a small read-only ownership seam to preserve decoupling.
 */
UCLASS(meta = (DisplayName = "Owned Zone"))
class DESIGNPATTERNSSIMGRID_API USimGrid_Rule_OwnedZone : public USimGrid_PlacementRuleStrategy
{
	GENERATED_BODY()

public:
	/**
	 * When true, a cell with NO owner (neutral) also fails the rule (you must own it). When false, only
	 * cells owned by a DIFFERENT identity fail; unowned cells are permitted.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement|Ownership")
	bool bRequireExplicitOwnership = true;

	virtual FSimGrid_RuleResult Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const override;
};
