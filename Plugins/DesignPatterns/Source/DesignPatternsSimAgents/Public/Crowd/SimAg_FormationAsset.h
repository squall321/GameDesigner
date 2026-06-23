// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "SimAg_FormationAsset.generated.h"

/**
 * A tag-identified formation pattern: an ordered list of slot offsets (relative to a leader/anchor, in the
 * anchor's local space). Slot 0 is the leader's own slot by convention. Used by the formation subsystem to
 * resolve a group member's stand position. Genre-neutral; resolved by tag through the data registry.
 *
 * If SlotOffsets is empty, the subsystem falls back to a procedural grid using the settings FormationSpacing.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMAGENTS_API USimAg_FormationAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimAg_FormationAsset();

	/**
	 * Slot offsets in the anchor's local space (X forward, Y right). Authored per formation shape (wedge,
	 * column, line, circle...). Empty = procedural grid fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd")
	TArray<FVector> SlotOffsets;

	/**
	 * Number of slots per row used by the procedural grid fallback when SlotOffsets is empty. >=1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd", meta = (ClampMin = "1"))
	int32 GridColumns = 4;

	/**
	 * Resolve the local-space offset for SlotIndex. Uses SlotOffsets when authored; otherwise a procedural
	 * grid with the given spacing (world units). SlotIndex is clamped/ wrapped sensibly.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FVector GetSlotOffset(int32 SlotIndex, float Spacing) const;

	/** Number of authored slots (0 when relying on the procedural grid). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	int32 NumAuthoredSlots() const { return SlotOffsets.Num(); }

	//~ Begin UDP_DataAsset
	/** Group all formation assets under one asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
