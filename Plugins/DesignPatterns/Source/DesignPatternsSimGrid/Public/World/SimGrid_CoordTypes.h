// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_CoordTypes.generated.h"

/**
 * Coordinate of a fixed-size chunk in the grid. A chunk groups a ChunkSize x ChunkSize block of
 * cells under one replication carrier (ASimGrid_ChunkReplicator) so the world streams/dormancy-gates
 * grid state at chunk granularity instead of per-cell. Hashable for use as a TMap key.
 *
 * NOTE: the per-CELL coordinate type is FSeam_CellCoord, owned by the Seams module, so read-only
 * consumers (agents, economy) never depend on SimGrid. This chunk type is SimGrid-internal.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_ChunkCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SimGrid|Coord")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SimGrid|Coord")
	int32 Y = 0;

	FSimGrid_ChunkCoord() = default;
	FSimGrid_ChunkCoord(int32 InX, int32 InY) : X(InX), Y(InY) {}

	bool operator==(const FSimGrid_ChunkCoord& Other) const { return X == Other.X && Y == Other.Y; }
	bool operator!=(const FSimGrid_ChunkCoord& Other) const { return !(*this == Other); }

	FSimGrid_ChunkCoord operator+(const FSimGrid_ChunkCoord& Other) const
	{
		return FSimGrid_ChunkCoord(X + Other.X, Y + Other.Y);
	}

	friend uint32 GetTypeHash(const FSimGrid_ChunkCoord& C)
	{
		return HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y));
	}

	FString ToString() const { return FString::Printf(TEXT("Chunk(%d, %d)"), X, Y); }
};

/**
 * Discrete cardinal rotation for a placed tile/footprint, in 90-degree steps. Footprints rotate by
 * permuting their local cell offsets through one of these four orientations; storing a discrete enum
 * (rather than a float yaw) keeps placement deterministic and trivially replicable.
 */
UENUM(BlueprintType)
enum class ESimGrid_Rotation : uint8
{
	R0   UMETA(DisplayName = "0 deg"),
	R90  UMETA(DisplayName = "90 deg"),
	R180 UMETA(DisplayName = "180 deg"),
	R270 UMETA(DisplayName = "270 deg")
};

/**
 * Neighbourhood model for adjacency / flood-fill / line queries: 4-connected (von Neumann, edges only)
 * or 8-connected (Moore, edges + diagonals).
 */
UENUM(BlueprintType)
enum class ESimGrid_Adjacency : uint8
{
	/** Up/Down/Left/Right only (4 neighbours). */
	Four UMETA(DisplayName = "Four (Von Neumann)"),
	/** Edges plus diagonals (8 neighbours). */
	Eight UMETA(DisplayName = "Eight (Moore)")
};

/**
 * Pure, allocation-free helpers for grid coordinate math. All functions are static and side-effect
 * free so they are safe on clients and inside hot query loops. Cell<->chunk math uses floor division
 * so negative coordinates map to the correct chunk (C++ integer division truncates toward zero, which
 * is wrong for negatives — these helpers correct for it).
 */
struct DESIGNPATTERNSSIMGRID_API FSimGrid_CoordMath
{
	/** Floor-divide that rounds toward negative infinity (so -1 / 8 == -1, not 0). */
	static FORCEINLINE int32 FloorDiv(int32 A, int32 B)
	{
		check(B != 0);
		const int32 Q = A / B;
		const int32 R = A % B;
		return (R != 0 && ((R < 0) != (B < 0))) ? Q - 1 : Q;
	}

	/** Positive modulo that wraps into [0, B) even for negative A. */
	static FORCEINLINE int32 PosMod(int32 A, int32 B)
	{
		check(B != 0);
		const int32 M = A % B;
		return (M < 0) ? M + FMath::Abs(B) : M;
	}

	/** The chunk that owns a given cell, for a square chunk of side ChunkSize (clamped to >= 1). */
	static FSimGrid_ChunkCoord CellToChunk(const FSeam_CellCoord& Cell, const FIntPoint& ChunkSize)
	{
		const int32 SX = FMath::Max(1, ChunkSize.X);
		const int32 SY = FMath::Max(1, ChunkSize.Y);
		return FSimGrid_ChunkCoord(FloorDiv(Cell.X, SX), FloorDiv(Cell.Y, SY));
	}

	/** Cell offset of a cell WITHIN its chunk, in [0,ChunkSize) on each axis. */
	static FSeam_CellCoord CellToLocal(const FSeam_CellCoord& Cell, const FIntPoint& ChunkSize)
	{
		const int32 SX = FMath::Max(1, ChunkSize.X);
		const int32 SY = FMath::Max(1, ChunkSize.Y);
		return FSeam_CellCoord(PosMod(Cell.X, SX), PosMod(Cell.Y, SY));
	}

	/** The world-space cell origin (min corner) of a chunk's (0,0) cell. */
	static FSeam_CellCoord ChunkOriginCell(const FSimGrid_ChunkCoord& Chunk, const FIntPoint& ChunkSize)
	{
		const int32 SX = FMath::Max(1, ChunkSize.X);
		const int32 SY = FMath::Max(1, ChunkSize.Y);
		return FSeam_CellCoord(Chunk.X * SX, Chunk.Y * SY);
	}

	/** Apply a discrete rotation to an integer offset around the grid origin. */
	static FSeam_CellCoord RotateOffset(const FSeam_CellCoord& Offset, ESimGrid_Rotation Rotation)
	{
		switch (Rotation)
		{
		case ESimGrid_Rotation::R90:  return FSeam_CellCoord(-Offset.Y,  Offset.X);
		case ESimGrid_Rotation::R180: return FSeam_CellCoord(-Offset.X, -Offset.Y);
		case ESimGrid_Rotation::R270: return FSeam_CellCoord( Offset.Y, -Offset.X);
		case ESimGrid_Rotation::R0:
		default:                      return Offset;
		}
	}

	/** Yaw in degrees corresponding to a discrete rotation, for orienting meshes/actors. */
	static FORCEINLINE float RotationToYaw(ESimGrid_Rotation Rotation)
	{
		return 90.f * static_cast<float>(static_cast<uint8>(Rotation));
	}

	/** Chebyshev (king-move / 8-connected) distance between two cells. */
	static FORCEINLINE int32 ChebyshevDistance(const FSeam_CellCoord& A, const FSeam_CellCoord& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}

	/** Manhattan (4-connected) distance between two cells. */
	static FORCEINLINE int32 ManhattanDistance(const FSeam_CellCoord& A, const FSeam_CellCoord& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}

	/**
	 * Fill OutNeighbours with the 4 or 8 neighbours of Cell according to Adjacency. Clears OutNeighbours
	 * first. Diagonals are appended after the cardinals so callers can take the first 4 for either model.
	 */
	static void GetNeighbours(const FSeam_CellCoord& Cell, ESimGrid_Adjacency Adjacency,
		TArray<FSeam_CellCoord>& OutNeighbours)
	{
		OutNeighbours.Reset(Adjacency == ESimGrid_Adjacency::Eight ? 8 : 4);
		OutNeighbours.Add(FSeam_CellCoord(Cell.X + 1, Cell.Y));
		OutNeighbours.Add(FSeam_CellCoord(Cell.X - 1, Cell.Y));
		OutNeighbours.Add(FSeam_CellCoord(Cell.X, Cell.Y + 1));
		OutNeighbours.Add(FSeam_CellCoord(Cell.X, Cell.Y - 1));
		if (Adjacency == ESimGrid_Adjacency::Eight)
		{
			OutNeighbours.Add(FSeam_CellCoord(Cell.X + 1, Cell.Y + 1));
			OutNeighbours.Add(FSeam_CellCoord(Cell.X + 1, Cell.Y - 1));
			OutNeighbours.Add(FSeam_CellCoord(Cell.X - 1, Cell.Y + 1));
			OutNeighbours.Add(FSeam_CellCoord(Cell.X - 1, Cell.Y - 1));
		}
	}
};
