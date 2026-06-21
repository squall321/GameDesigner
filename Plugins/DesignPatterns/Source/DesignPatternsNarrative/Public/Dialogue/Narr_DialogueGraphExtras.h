// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Narr_DialogueGraphExtras.generated.h"

/**
 * Per-node presentation/behaviour extras that the shipped FNarr_DialogueNode does not carry.
 *
 * Authored in a SIDE-CAR data asset keyed to a graph (see UNarr_DialogueGraphExtras) so the shipped
 * UNarr_DialogueGraph / FNarr_DialogueNode stay frozen. Holds a speaker emotion tag (drives portrait/VO),
 * an optional portrait override, and one-time-vs-repeatable flags for nodes that should only play once.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueNodeExtras
{
	GENERATED_BODY()

	/** Emotion of the speaker on this node (Narr.Emotion.*), for portrait/VO/animation selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue", meta = (Categories = "Narr.Emotion"))
	FGameplayTag EmotionTag;

	/** Optional soft portrait override for this node (the presenter loads it when shown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	TSoftObjectPtr<UObject> PortraitOverride;

	/**
	 * When true this node plays only ONCE per save: a runner consults the dialogue history (HasSeenNode) and
	 * skips/diverts a one-time node it has already shown. Repeatable nodes (the default) always play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	bool bOneTime = false;

	/** Optional camera/shot tag the presenter may use to frame this node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue", meta = (Categories = "Narr.Shot"))
	FGameplayTag ShotTag;
};

/**
 * A side-car data asset holding per-node extras for ONE dialogue graph, resolved by a DataTag equal to the
 * graph's DataTag (deterministic, load-free lookup through the data registry).
 *
 * This is the additive way to enrich dialogue presentation WITHOUT editing the shipped graph: the presenter
 * / history component look up the extras for the active graph by its DataTag and, if found, apply the
 * per-node emotion/portrait/one-time data. An absent side-car asset is a graceful no-op (default emotion,
 * no portrait, all nodes repeatable).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSNARRATIVE_API UNarr_DialogueGraphExtras : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Per-node extras, keyed by the node's NodeId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	TMap<FGameplayTag, FNarr_DialogueNodeExtras> NodeExtras;

	//~ Begin UDP_DataAsset
	/** Own asset-manager bucket ("Narr_DialogueExtras") so these resolve independently of graphs. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** @return the extras for NodeId, or null if this graph has no extras for that node. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Dialogue")
	const FNarr_DialogueNodeExtras* FindExtras(FGameplayTag NodeId) const;

	/**
	 * Resolve the extras asset whose DataTag matches GraphDataTag from the data registry. @return the asset
	 * or null. Static so presenters/history can resolve without holding a reference.
	 */
	static UNarr_DialogueGraphExtras* ResolveForGraph(const UObject* WorldContext, FGameplayTag GraphDataTag);
};
