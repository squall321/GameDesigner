// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_PlacementTypes.generated.h"

class ISeam_TileProviderRead;

/**
 * Discrete 90-degree rotation of a placeable footprint on the grid. SimGrid is a top-down 2D cell
 * world, so rotation is restricted to the four cardinal yaw steps; footprint offsets are rotated by
 * this amount when a placement is evaluated or committed. Stored compactly (uint8) so it can ride in
 * a fast-array item and replicate cheaply.
 */
UENUM(BlueprintType)
enum class ESimGrid_Rotation : uint8
{
	/** No rotation (0 degrees). */
	None UMETA(DisplayName = "0"),
	/** 90 degrees clockwise (+X maps to +Y). */
	CW90 UMETA(DisplayName = "90"),
	/** 180 degrees. */
	CW180 UMETA(DisplayName = "180"),
	/** 270 degrees clockwise / 90 degrees counter-clockwise. */
	CW270 UMETA(DisplayName = "270")
};

/**
 * Overall verdict for a candidate placement. ValidatePlacement aggregates the per-rule
 * FSimGrid_RuleResult values into one of these so callers (ghost preview tinting, UI gating, the
 * server commit path) can branch on a single client-safe enum without re-running the rules.
 */
UENUM(BlueprintType)
enum class ESimGrid_PlacementValidity : uint8
{
	/** Every rule passed; the placement may be committed on the server. */
	Valid,
	/** At least one rule failed authoritatively (the cell state was known and disallowed). */
	Invalid,
	/**
	 * The local machine lacks enough replicated grid data to decide (a rule hit an Unknown cell on a
	 * client). The server will re-evaluate authoritatively; the client must not treat this as Valid.
	 */
	Unknown
};

/**
 * One footprint cell of a placeable, expressed as an offset from the placement origin together with
 * the terrain the cell requires. Offsets are pre-rotation; the placement system rotates them by the
 * candidate ESimGrid_Rotation before testing against the grid. An empty RequiredTerrain container
 * means "any terrain is acceptable for this cell".
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_FootprintCell
{
	GENERATED_BODY()

	/** Cell offset from the placement origin, before rotation is applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SimGrid|Placement")
	FSeam_CellCoord RelativeOffset;

	/**
	 * Terrain tags this footprint cell requires. If non-empty, the grid cell's tile-type tag must
	 * match (any) one of these for the cell to be placeable; empty means no terrain constraint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SimGrid|Placement")
	FGameplayTagContainer RequiredTerrain;

	FSimGrid_FootprintCell() = default;
	FSimGrid_FootprintCell(const FSeam_CellCoord& InOffset, const FGameplayTagContainer& InTerrain)
		: RelativeOffset(InOffset), RequiredTerrain(InTerrain) {}
};

/**
 * Everything a placement rule needs to judge a candidate placement, with no dependency on the
 * concrete grid implementation: the grid is reached only through the read seam. The Footprint is the
 * placeable's already-collected footprint (pre-rotation offsets); rules apply Rotation themselves via
 * the helper on this struct so every rule rotates consistently.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_PlacementContext
{
	GENERATED_BODY()

	/** Read-only grid seam the rules query. Never a hard SimGrid pointer, so rules stay decoupled. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	TScriptInterface<ISeam_TileProviderRead> Grid;

	/** Origin cell the footprint is anchored at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement")
	FSeam_CellCoord Origin;

	/** Rotation applied to every footprint offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement")
	ESimGrid_Rotation Rotation = ESimGrid_Rotation::None;

	/**
	 * Identity of the placement's owner (e.g. a player/faction tag). Ownership rules compare cell
	 * ownership against this; an invalid tag means "unowned / neutral placement".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Placement")
	FGameplayTag OwnerId;

	/** Footprint cells (pre-rotation offsets) of the placeable being tested. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	TArray<FSimGrid_FootprintCell> Footprint;

	FSimGrid_PlacementContext() = default;

	/** Rotate a single pre-rotation offset by this context's Rotation into a grid-space offset. */
	FSeam_CellCoord RotateOffset(const FSeam_CellCoord& Offset) const
	{
		return RotateOffsetBy(Offset, Rotation);
	}

	/** Absolute (world-grid) cell for a footprint entry, i.e. Origin + rotated offset. */
	FSeam_CellCoord ResolveCell(const FSimGrid_FootprintCell& Cell) const
	{
		return Origin + RotateOffset(Cell.RelativeOffset);
	}

	/** Pure rotation of an integer cell offset by a discrete rotation. Used by rules and previews. */
	static FSeam_CellCoord RotateOffsetBy(const FSeam_CellCoord& Offset, ESimGrid_Rotation Rotation)
	{
		switch (Rotation)
		{
		case ESimGrid_Rotation::CW90:  return FSeam_CellCoord(-Offset.Y,  Offset.X);
		case ESimGrid_Rotation::CW180: return FSeam_CellCoord(-Offset.X, -Offset.Y);
		case ESimGrid_Rotation::CW270: return FSeam_CellCoord( Offset.Y, -Offset.X);
		case ESimGrid_Rotation::None:
		default:                       return Offset;
		}
	}
};

/**
 * The result of evaluating ONE placement rule against a context. Carries the validity verdict, a
 * designer-facing reason tag (so UI can localize / show an icon for the failure), and the specific
 * cells the rule found offending (for highlighting). A rule that cannot decide locally (client lacks
 * replicated state) returns Unknown so the aggregate can stay conservative.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_RuleResult
{
	GENERATED_BODY()

	/** This rule's verdict. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	ESimGrid_PlacementValidity Validity = ESimGrid_PlacementValidity::Valid;

	/**
	 * Optional reason tag describing why the rule failed (e.g. SimGrid.Placement.Fail.OutOfBounds).
	 * Empty when the rule passed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	FGameplayTag ReasonTag;

	/** Cells the rule found offending, for ghost/UI highlighting. Empty when the rule passed. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	TArray<FSeam_CellCoord> OffendingCells;

	FSimGrid_RuleResult() = default;

	/** Construct a passing result. */
	static FSimGrid_RuleResult MakeValid()
	{
		return FSimGrid_RuleResult();
	}

	/** Construct a failing result with a reason and optional offending cells. */
	static FSimGrid_RuleResult MakeInvalid(const FGameplayTag& Reason, TArray<FSeam_CellCoord> Cells = {})
	{
		FSimGrid_RuleResult R;
		R.Validity = ESimGrid_PlacementValidity::Invalid;
		R.ReasonTag = Reason;
		R.OffendingCells = MoveTemp(Cells);
		return R;
	}

	/** Construct an indeterminate result (client lacked replicated state). */
	static FSimGrid_RuleResult MakeUnknown(const FGameplayTag& Reason, TArray<FSeam_CellCoord> Cells = {})
	{
		FSimGrid_RuleResult R;
		R.Validity = ESimGrid_PlacementValidity::Unknown;
		R.ReasonTag = Reason;
		R.OffendingCells = MoveTemp(Cells);
		return R;
	}

	bool IsValid() const { return Validity == ESimGrid_PlacementValidity::Valid; }
};

/**
 * The aggregate outcome of validating a whole placement: the combined validity plus the merged set of
 * offending cells and the first failing reason, suitable for driving a ghost preview and gating the
 * commit. ValidatePlacement returns this; the server commit path re-derives it authoritatively rather
 * than trusting a client-supplied copy.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_PlacementResult
{
	GENERATED_BODY()

	/**
	 * Aggregate verdict: Valid only if every rule was Valid; Unknown if any rule was Unknown and none
	 * Invalid. Starts Valid and is degraded by Accumulate as failing rules fold in (worst-wins). A
	 * placement validated against an empty rule set is therefore trivially Valid.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	ESimGrid_PlacementValidity Validity = ESimGrid_PlacementValidity::Valid;

	/** Reason tag of the first non-passing rule (Invalid preferred over Unknown). Empty when Valid. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	FGameplayTag FirstFailureReason;

	/** Union of all offending cells reported by failing rules, for highlighting. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	TArray<FSeam_CellCoord> OffendingCells;

	/** The absolute cells the footprint resolves to (Origin + rotated offsets), valid or not. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Placement")
	TArray<FSeam_CellCoord> ResolvedCells;

	FSimGrid_PlacementResult() = default;

	bool IsValid() const { return Validity == ESimGrid_PlacementValidity::Valid; }

	/** Fold one rule result into this aggregate, tracking the worst validity and first failure reason. */
	void Accumulate(const FSimGrid_RuleResult& RuleResult)
	{
		// Worst-wins ordering: Invalid dominates Unknown dominates Valid.
		const auto Severity = [](ESimGrid_PlacementValidity V) -> int32
		{
			switch (V)
			{
			case ESimGrid_PlacementValidity::Invalid: return 2;
			case ESimGrid_PlacementValidity::Unknown: return 1;
			default:                                  return 0;
			}
		};

		if (Severity(RuleResult.Validity) > Severity(Validity))
		{
			Validity = RuleResult.Validity;
		}

		if (!RuleResult.IsValid())
		{
			if (!FirstFailureReason.IsValid() && RuleResult.ReasonTag.IsValid())
			{
				FirstFailureReason = RuleResult.ReasonTag;
			}
			OffendingCells.Append(RuleResult.OffendingCells);
		}
	}
};
