// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/SimGrid_PlacementRuleStrategy.h"
#include "SimGrid_NativeTags.h"
#include "Core/DPLog.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Grid/Seam_GridCoord.h"
#include "Territory/SimGrid_TerritoryTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/ScriptInterface.h"

//~ Base ---------------------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_PlacementRuleStrategy::Evaluate_Implementation(const FSimGrid_PlacementContext& /*Context*/) const
{
	// An unconfigured custom rule is non-blocking by default.
	return FSimGrid_RuleResult::MakeValid();
}

//~ Shared helpers (file-local) ----------------------------------------------------------------

namespace
{
	/** Resolve the cell snapshot through the read seam, or an Unknown snapshot if the grid is null. */
	FSeam_CellSnapshot SnapshotOf(const FSimGrid_PlacementContext& Context, const FSeam_CellCoord& Cell)
	{
		if (Context.Grid.GetObject())
		{
			return ISeam_TileProviderRead::Execute_GetCellSnapshot(Context.Grid.GetObject(), Cell);
		}
		return FSeam_CellSnapshot{};
	}

	bool IsValidCellSeam(const FSimGrid_PlacementContext& Context, const FSeam_CellCoord& Cell)
	{
		return Context.Grid.GetObject() && ISeam_TileProviderRead::Execute_IsValidCell(Context.Grid.GetObject(), Cell);
	}
}

//~ InBounds -----------------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_Rule_InBounds::Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const
{
	if (!Context.Grid.GetObject())
	{
		// No grid resolved: cannot decide bounds — conservative Unknown so the server re-checks.
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}

	TArray<FSeam_CellCoord> Offending;
	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		const FSeam_CellCoord Cell = Context.ResolveCell(FC);
		if (!IsValidCellSeam(Context, Cell))
		{
			Offending.Add(Cell);
		}
	}

	if (Offending.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeInvalid(SimGridTags::Fail_OutOfBounds, MoveTemp(Offending));
	}
	return FSimGrid_RuleResult::MakeValid();
}

//~ CellsEmpty ---------------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_Rule_CellsEmpty::Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const
{
	if (!Context.Grid.GetObject())
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}

	TArray<FSeam_CellCoord> Occupied;
	TArray<FSeam_CellCoord> UnknownCells;

	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		const FSeam_CellCoord Cell = Context.ResolveCell(FC);
		const FSeam_CellSnapshot Snap = SnapshotOf(Context, Cell);

		if (!Snap.IsKnown())
		{
			UnknownCells.Add(Cell);
		}
		else if (Snap.IsSet())
		{
			Occupied.Add(Cell);
		}
	}

	// An authoritatively-occupied cell is a hard failure regardless of unknowns elsewhere.
	if (Occupied.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeInvalid(SimGridTags::Fail_CellOccupied, MoveTemp(Occupied));
	}
	// Otherwise, if any cell was unreplicated, we cannot confirm emptiness locally.
	if (UnknownCells.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown, MoveTemp(UnknownCells));
	}
	return FSimGrid_RuleResult::MakeValid();
}

//~ TerrainAllowed -----------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_Rule_TerrainAllowed::Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const
{
	if (!Context.Grid.GetObject())
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}

	TArray<FSeam_CellCoord> Offending;
	TArray<FSeam_CellCoord> UnknownCells;

	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		const FSeam_CellCoord Cell = Context.ResolveCell(FC);
		const FSeam_CellSnapshot Snap = SnapshotOf(Context, Cell);

		if (!Snap.IsKnown())
		{
			UnknownCells.Add(Cell);
			continue;
		}

		// A known-empty cell has no tile-type tag. If a terrain constraint exists, an empty cell fails
		// it (there is no terrain to satisfy the requirement).
		const FGameplayTag TerrainTag = Snap.TileTypeTag;

		// Blocked list always wins.
		if (BlockedTerrain.Num() > 0 && TerrainTag.IsValid() && TerrainTag.MatchesAny(BlockedTerrain))
		{
			Offending.Add(Cell);
			continue;
		}

		// Effective allow-list = the rule's AllowedTerrain combined with this footprint cell's
		// per-cell RequiredTerrain. If either is specified, the terrain must satisfy it.
		const bool bHasRuleAllow = AllowedTerrain.Num() > 0;
		const bool bHasCellAllow = FC.RequiredTerrain.Num() > 0;

		if (bHasRuleAllow && !(TerrainTag.IsValid() && TerrainTag.MatchesAny(AllowedTerrain)))
		{
			Offending.Add(Cell);
			continue;
		}
		if (bHasCellAllow && !(TerrainTag.IsValid() && TerrainTag.MatchesAny(FC.RequiredTerrain)))
		{
			Offending.Add(Cell);
			continue;
		}
	}

	if (Offending.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeInvalid(SimGridTags::Fail_TerrainNotAllowed, MoveTemp(Offending));
	}
	if (UnknownCells.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown, MoveTemp(UnknownCells));
	}
	return FSimGrid_RuleResult::MakeValid();
}

//~ RequireAdjacent ----------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_Rule_RequireAdjacent::Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const
{
	if (!Context.Grid.GetObject())
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}
	if (RequiredAdjacentTiles.Num() == 0 || RequiredCount <= 0)
	{
		// Nothing to require.
		return FSimGrid_RuleResult::MakeValid();
	}

	// Collect the absolute footprint cells (so we can exclude them from the perimeter scan).
	TSet<FSeam_CellCoord> FootprintCells;
	FootprintCells.Reserve(Context.Footprint.Num());
	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		FootprintCells.Add(Context.ResolveCell(FC));
	}

	// Build the unique perimeter set: every neighbour of a footprint cell that is not itself in the
	// footprint. This avoids double-counting shared neighbours.
	static const FSeam_CellCoord Ortho[4] =
	{
		FSeam_CellCoord(1, 0), FSeam_CellCoord(-1, 0), FSeam_CellCoord(0, 1), FSeam_CellCoord(0, -1)
	};
	static const FSeam_CellCoord Diag[4] =
	{
		FSeam_CellCoord(1, 1), FSeam_CellCoord(1, -1), FSeam_CellCoord(-1, 1), FSeam_CellCoord(-1, -1)
	};

	TSet<FSeam_CellCoord> Perimeter;
	for (const FSeam_CellCoord& Cell : FootprintCells)
	{
		for (const FSeam_CellCoord& D : Ortho)
		{
			const FSeam_CellCoord N = Cell + D;
			if (!FootprintCells.Contains(N))
			{
				Perimeter.Add(N);
			}
		}
		if (bIncludeDiagonals)
		{
			for (const FSeam_CellCoord& D : Diag)
			{
				const FSeam_CellCoord N = Cell + D;
				if (!FootprintCells.Contains(N))
				{
					Perimeter.Add(N);
				}
			}
		}
	}

	int32 Satisfying = 0;
	bool bAnyUnknown = false;
	TArray<FSeam_CellCoord> Matched;

	for (const FSeam_CellCoord& N : Perimeter)
	{
		const FSeam_CellSnapshot Snap = SnapshotOf(Context, N);
		if (!Snap.IsKnown())
		{
			bAnyUnknown = true;
			continue;
		}
		if (Snap.IsSet() && Snap.TileTypeTag.IsValid() && Snap.TileTypeTag.MatchesAny(RequiredAdjacentTiles))
		{
			++Satisfying;
			Matched.Add(N);
			if (Satisfying >= RequiredCount)
			{
				return FSimGrid_RuleResult::MakeValid();
			}
		}
	}

	// Requirement not met on what we can see. If some neighbours were unknown, the server might still
	// satisfy it — report Unknown; otherwise it is a hard failure.
	if (bAnyUnknown)
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}
	return FSimGrid_RuleResult::MakeInvalid(SimGridTags::Fail_MissingAdjacency);
}

//~ OwnedZone ----------------------------------------------------------------------------------

FSimGrid_RuleResult USimGrid_Rule_OwnedZone::Evaluate_Implementation(const FSimGrid_PlacementContext& Context) const
{
	// Resolve the ownership read seam off the grid-provider's owning actor (decoupled from the
	// concrete territory component). The grid seam object is usually a subsystem or an actor; we walk
	// to an actor that implements ISimGrid_OwnershipRead.
	UObject* GridObj = Context.Grid.GetObject();
	UObject* OwnershipObj = nullptr;

	if (GridObj && GridObj->GetClass()->ImplementsInterface(USimGrid_OwnershipRead::StaticClass()))
	{
		OwnershipObj = GridObj;
	}
	else if (AActor* GridActor = Cast<AActor>(GridObj))
	{
		// Look for an ownership component on the same actor.
		if (UActorComponent* Comp = GridActor->FindComponentByInterface(USimGrid_OwnershipRead::StaticClass()))
		{
			OwnershipObj = Comp;
		}
	}

	if (!OwnershipObj)
	{
		// We could not resolve ownership locally; the server will re-check authoritatively.
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown);
	}

	TArray<FSeam_CellCoord> Offending;
	TArray<FSeam_CellCoord> UnknownCells;

	for (const FSimGrid_FootprintCell& FC : Context.Footprint)
	{
		const FSeam_CellCoord Cell = Context.ResolveCell(FC);

		if (!ISimGrid_OwnershipRead::Execute_IsOwnershipKnown(OwnershipObj, Cell))
		{
			UnknownCells.Add(Cell);
			continue;
		}

		const FGameplayTag CellOwner = ISimGrid_OwnershipRead::Execute_GetCellOwner(OwnershipObj, Cell);

		if (!CellOwner.IsValid())
		{
			// Unowned cell. Fails only when explicit ownership is required.
			if (bRequireExplicitOwnership)
			{
				Offending.Add(Cell);
			}
		}
		else if (CellOwner != Context.OwnerId)
		{
			// Owned by someone else: always a failure.
			Offending.Add(Cell);
		}
	}

	if (Offending.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeInvalid(SimGridTags::Fail_NotOwnedZone, MoveTemp(Offending));
	}
	if (UnknownCells.Num() > 0)
	{
		return FSimGrid_RuleResult::MakeUnknown(SimGridTags::Fail_StateUnknown, MoveTemp(UnknownCells));
	}
	return FSimGrid_RuleResult::MakeValid();
}
