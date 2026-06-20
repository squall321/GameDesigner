// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Seam/InvUI_ItemContainer.h"
#include "Seam/InvUI_ItemDisplay.h"
#include "InvUI_SortStrategy.generated.h"

/** Ascending or descending ordering applied on top of a sort strategy's key. */
UENUM(BlueprintType)
enum class EInvUI_SortDirection : uint8
{
	Ascending,
	Descending
};

/**
 * Pluggable, data-driven strategy that orders a list of slot states for *display*.
 *
 * This is a pure presentation reorder: it never mutates the backend (the authoritative slot
 * identities/contents are unchanged) and never assumes a slot count. A window picks one of the
 * shipped EditInlineNew subclasses (or a project subclass) as a UPROPERTY(Instanced); the grid
 * viewmodel calls SortSlots to produce the display order. An optional IInvUI_ItemDisplay
 * resolver is passed so name-based sorting can use localized display names when they are already
 * cached (it must NOT block on async loads — unresolved names fall back to the item tag).
 *
 * Authoring a strategy: override CompareSlots to return <0 / 0 / >0 for (A before B / equal /
 * A after B) in ASCENDING terms; the base applies Direction and a stable tie-break by SlotTag so
 * the order is deterministic. Empty slots always sort to the end regardless of direction.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, BlueprintType, CollapseCategories,
	meta = (DisplayName = "InvUI Sort Strategy"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SortStrategy : public UObject
{
	GENERATED_BODY()

public:
	/** Ascending (default) or descending application of the comparison key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Sort")
	EInvUI_SortDirection Direction = EInvUI_SortDirection::Ascending;

	/**
	 * Reorder InOutSlots in place. The base implements the full stable sort (direction handling,
	 * empty-slots-last, SlotTag tie-break) by delegating per-pair comparisons to CompareSlots.
	 * DisplayResolver may be empty; when present it is consulted only via the cached fast path.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Sort")
	virtual void SortSlots(UPARAM(ref) TArray<FInvUI_SlotState>& InOutSlots,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const;

	/**
	 * Three-way comparison of two OCCUPIED slots in ascending terms: negative if A should come
	 * before B, positive if after, zero if equal on this strategy's key. Empty slots are handled
	 * by the base and never reach here. DisplayResolver may be empty.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Sort")
	int32 CompareSlots(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const;
	virtual int32 CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const;

protected:
	/**
	 * Resolve a slot's display name through the cached fast path of DisplayResolver, falling back
	 * to the item tag's leaf name when no cached info exists. Never triggers an async load.
	 */
	static FString GetCachedSortName(const FInvUI_SlotState& Slot,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver);
};

/**
 * Sort by display name (A→Z ascending). Uses the resolver's cached localized name when present,
 * otherwise the item tag's leaf string. Good default for bags.
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Sort By Name"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SortByName : public UInvUI_SortStrategy
{
	GENERATED_BODY()

public:
	virtual int32 CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const override;
};

/** Sort by stack count (fewest→most ascending), tie-broken by name then SlotTag (in the base). */
UCLASS(EditInlineNew, meta = (DisplayName = "Sort By Count"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SortByCount : public UInvUI_SortStrategy
{
	GENERATED_BODY()

public:
	virtual int32 CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const override;
};

/**
 * Sort by item *type*, grouping items whose ItemTag shares a configured ancestor, then by name
 * within a group. The type order is data-driven: TypeOrder lists category tags most-significant
 * first; an item is bucketed by the first TypeOrder entry it matches (hierarchy-aware), with
 * unmatched items sorted to a trailing bucket. NO slot/category counts are hardcoded.
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Sort By Type"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SortByType : public UInvUI_SortStrategy
{
	GENERATED_BODY()

public:
	/**
	 * Ordered category tags (most-significant first). An item is bucketed by the first entry its
	 * ItemTag matches (MatchesTag, so a parent category matches all its children). Items matching
	 * no entry are placed after all listed categories.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Sort")
	FGameplayTagContainer TypeOrder;

	virtual int32 CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const override;

private:
	/** Index of the first TypeOrder entry that matches Tag, or TypeOrder.Num() if none. */
	int32 BucketIndexFor(const FGameplayTag& Tag) const;
};
