// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/InvUI_SearchSortStrategy.h"

namespace
{
	/**
	 * Read a slot's display name through the resolver's cached fast path (never async), falling back
	 * to the item-tag leaf name. Mirrors UInvUI_SortStrategy::GetCachedSortName so the filter and the
	 * relevance sort agree on what "the name" is.
	 */
	FString ResolveCachedName(const FInvUI_SlotState& Slot,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver)
	{
		if (DisplayResolver.GetObject() != nullptr && Slot.ItemTag.IsValid())
		{
			FInvUI_ItemDisplayInfo Info;
			if (IInvUI_ItemDisplay::Execute_TryGetCachedDisplay(DisplayResolver.GetObject(), Slot.ItemTag, Info)
				&& Info.bResolved && !Info.DisplayName.IsEmpty())
			{
				return Info.DisplayName.ToString();
			}
		}

		// Fall back to the leaf segment of the item tag (e.g. "Item.Weapon.Sword" -> "Sword").
		FString Full = Slot.ItemTag.ToString();
		int32 LastDot = INDEX_NONE;
		if (Full.FindLastChar(TEXT('.'), LastDot))
		{
			return Full.RightChop(LastDot + 1);
		}
		return Full;
	}
}

int32 UInvUI_SortBySearchRelevance::RelevanceScore(const FString& Name) const
{
	if (SearchTerm.IsEmpty())
	{
		return 0; // no query -> everything equally relevant; base falls back to the name tie-break
	}
	if (Name.Equals(SearchTerm, ESearchCase::IgnoreCase))
	{
		return 3;
	}
	if (Name.StartsWith(SearchTerm, ESearchCase::IgnoreCase))
	{
		return 2;
	}
	if (Name.Contains(SearchTerm, ESearchCase::IgnoreCase))
	{
		return 1;
	}
	return 0;
}

int32 UInvUI_SortBySearchRelevance::CompareSlots_Implementation(const FInvUI_SlotState& A,
	const FInvUI_SlotState& B, const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	const FString NameA = ResolveCachedName(A, DisplayResolver);
	const FString NameB = ResolveCachedName(B, DisplayResolver);

	const int32 ScoreA = RelevanceScore(NameA);
	const int32 ScoreB = RelevanceScore(NameB);

	// Higher relevance comes FIRST in ascending terms, so invert the score comparison.
	if (ScoreA != ScoreB)
	{
		return (ScoreA > ScoreB) ? -1 : 1;
	}

	// Same relevance bucket: order alphabetically (the base still applies Direction + SlotTag tie-break).
	return NameA.Compare(NameB, ESearchCase::IgnoreCase);
}

bool UInvUI_NameFilter::PassesFilter(const FInvUI_SlotState& Slot,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	if (Query.IsEmpty())
	{
		return true;
	}
	const FString Name = ResolveCachedName(Slot, DisplayResolver);
	return Name.Contains(Query, ESearchCase::IgnoreCase);
}

void UInvUI_NameFilter::FilterSlots(const TArray<FInvUI_SlotState>& In,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver, TArray<FInvUI_SlotState>& Out) const
{
	Out.Reset();
	Out.Reserve(In.Num());
	for (const FInvUI_SlotState& Slot : In)
	{
		if (PassesFilter(Slot, DisplayResolver))
		{
			Out.Add(Slot);
		}
	}
}
