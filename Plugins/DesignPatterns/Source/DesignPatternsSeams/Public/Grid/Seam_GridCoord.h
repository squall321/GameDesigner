// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Seam_GridCoord.generated.h"

/**
 * A 2D integer cell coordinate, owned by the Seams module so the read-only tile seam
 * (ISeam_TileProviderRead) and its consumers (SimAgents steering, SimEconomy facilities) can use it
 * without depending on the SimGrid module. SimGrid builds its full grid model on top of this type.
 * Hashable and comparable for use as a TMap key.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_CellCoord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam")
	int32 X = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam")
	int32 Y = 0;

	FSeam_CellCoord() = default;
	FSeam_CellCoord(int32 InX, int32 InY) : X(InX), Y(InY) {}

	bool operator==(const FSeam_CellCoord& Other) const { return X == Other.X && Y == Other.Y; }
	bool operator!=(const FSeam_CellCoord& Other) const { return !(*this == Other); }

	FSeam_CellCoord operator+(const FSeam_CellCoord& Other) const { return FSeam_CellCoord(X + Other.X, Y + Other.Y); }

	friend uint32 GetTypeHash(const FSeam_CellCoord& C)
	{
		return HashCombine(::GetTypeHash(C.X), ::GetTypeHash(C.Y));
	}

	FString ToString() const { return FString::Printf(TEXT("(%d, %d)"), X, Y); }
};

/** Whether a queried cell's state is actually known on this machine (clients may lack replication). */
UENUM(BlueprintType)
enum class ESeam_KnownState : uint8
{
	/** This machine has not received the cell's state (e.g. an out-of-relevance chunk on a client). */
	Unknown,
	/** The cell is known and empty. */
	Empty,
	/** The cell is known and has a tile/payload. */
	Set
};

/**
 * A tri-state read snapshot of one grid cell, returned by ISeam_TileProviderRead. The KnownState lets
 * a client distinguish "not replicated yet" from "definitely empty" so consumers don't treat missing
 * data as authoritative emptiness.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_CellSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam")
	ESeam_KnownState KnownState = ESeam_KnownState::Unknown;

	/** The tile-type tag when KnownState == Set; empty otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam")
	FGameplayTag TileTypeTag;

	bool IsKnown() const { return KnownState != ESeam_KnownState::Unknown; }
	bool IsSet() const { return KnownState == ESeam_KnownState::Set; }
};
