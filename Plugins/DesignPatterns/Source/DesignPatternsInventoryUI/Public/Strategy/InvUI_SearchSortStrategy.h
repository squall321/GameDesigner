// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "Strategy/InvUI_SortStrategy.h"
#include "Seam/InvUI_ItemDisplay.h"
#include "InvUI_SearchSortStrategy.generated.h"

/**
 * Sort strategy that ranks slots by how well their display name matches a live SearchTerm.
 *
 * Composes the EXISTING base: it overrides CompareSlots_Implementation (the real signature, taking
 * the IInvUI_ItemDisplay resolver) and ranks via the base's GetCachedSortName helper (never async,
 * per the base contract — unresolved names fall back to the item-tag leaf). The relevance score is
 * coarse and deterministic: an exact name match outranks a prefix match, which outranks a substring
 * match, which outranks "no match"; ties break by name then SlotTag (handled in the base). With an
 * empty SearchTerm it degenerates to a plain name sort, so the filter bar can keep one strategy
 * instance and just push the search text into it.
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Sort By Search Relevance"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SortBySearchRelevance : public UInvUI_SortStrategy
{
	GENERATED_BODY()

public:
	/** The current search query. Updated by the filter bar; empty = plain name sort. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Sort")
	FString SearchTerm;

	/** Set the search term and (caller) trigger a rebuild. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Sort")
	void SetSearchTerm(const FString& InSearchTerm) { SearchTerm = InSearchTerm; }

	//~ Begin UInvUI_SortStrategy
	virtual int32 CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const override;
	//~ End UInvUI_SortStrategy

private:
	/** 0 = no match, 1 = substring, 2 = prefix, 3 = exact (higher = more relevant). */
	int32 RelevanceScore(const FString& Name) const;
};

/**
 * Reusable name-substring predicate the FILTER BAR applies to the slot list BEFORE the grid
 * viewmodel rebuilds (the grid VM's SetItemFilter does hierarchy-aware TAG filtering; this does
 * free-text NAME filtering, which tags cannot express). It reads names through the cached fast
 * path of the supplied resolver (never blocking on an async load), falling back to the item-tag
 * leaf, exactly like the sort strategy. A UObject helper so it can be authored/owned by a VM.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Name Filter"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_NameFilter : public UObject
{
	GENERATED_BODY()

public:
	/** Set the case-insensitive substring a slot's name must contain. Empty = match everything. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Filter")
	void SetQuery(const FString& InQuery) { Query = InQuery; }

	/** The current query string. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Filter")
	const FString& GetQuery() const { return Query; }

	/** True when an empty query (match-everything) is active. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Filter")
	bool IsEmpty() const { return Query.IsEmpty(); }

	/** True when Slot's resolved name contains Query (case-insensitive). Empty query => true. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Filter")
	bool PassesFilter(const FInvUI_SlotState& Slot, const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const;

	/** Return only the slots whose name matches Query, preserving order. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Filter")
	void FilterSlots(const TArray<FInvUI_SlotState>& In, const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver,
		TArray<FInvUI_SlotState>& Out) const;

private:
	/** The active substring query (case-insensitive). */
	UPROPERTY(Transient)
	FString Query;
};
