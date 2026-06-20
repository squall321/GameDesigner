// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/InvUI_SortStrategy.h"
#include "Core/DPLog.h"

//================================ UInvUI_SortStrategy =================================

void UInvUI_SortStrategy::SortSlots(TArray<FInvUI_SlotState>& InOutSlots,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	const int32 DirSign = (Direction == EInvUI_SortDirection::Ascending) ? 1 : -1;

	// Stable sort: empty slots always last; occupied slots ordered by CompareSlots with a
	// deterministic SlotTag tie-break so the result never depends on input order.
	InOutSlots.StableSort([this, &DisplayResolver, DirSign](const FInvUI_SlotState& A, const FInvUI_SlotState& B)
	{
		const bool bAEmpty = A.IsEmpty();
		const bool bBEmpty = B.IsEmpty();
		if (bAEmpty != bBEmpty)
		{
			// Non-empty before empty, irrespective of direction.
			return !bAEmpty;
		}
		if (bAEmpty && bBEmpty)
		{
			// Both empty: stable order by slot tag.
			return A.SlotTag.ToString() < B.SlotTag.ToString();
		}

		int32 Cmp = CompareSlots(A, B, DisplayResolver) * DirSign;
		if (Cmp == 0)
		{
			// Deterministic final tie-break independent of direction.
			Cmp = A.SlotTag.ToString().Compare(B.SlotTag.ToString());
		}
		return Cmp < 0;
	});
}

int32 UInvUI_SortStrategy::CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	// Default key: display name. Subclasses override for count/type ordering.
	const FString NameA = GetCachedSortName(A, DisplayResolver);
	const FString NameB = GetCachedSortName(B, DisplayResolver);
	return NameA.Compare(NameB, ESearchCase::IgnoreCase);
}

FString UInvUI_SortStrategy::GetCachedSortName(const FInvUI_SlotState& Slot,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver)
{
	if (UObject* ResolverObj = DisplayResolver.GetObject())
	{
		if (DisplayResolver.GetInterface() != nullptr)
		{
			FInvUI_ItemDisplayInfo Info;
			if (IInvUI_ItemDisplay::Execute_TryGetCachedDisplay(ResolverObj, Slot.ItemTag, Info) &&
				Info.bResolved && !Info.DisplayName.IsEmpty())
			{
				return Info.DisplayName.ToString();
			}
		}
	}

	// Fallback: the leaf segment of the item tag (e.g. "Iron.Sword" -> "Sword").
	const FString TagStr = Slot.ItemTag.ToString();
	int32 LastDot = INDEX_NONE;
	if (TagStr.FindLastChar(TEXT('.'), LastDot))
	{
		return TagStr.RightChop(LastDot + 1);
	}
	return TagStr;
}

//================================ UInvUI_SortByName ==================================

int32 UInvUI_SortByName::CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	return GetCachedSortName(A, DisplayResolver).Compare(
		GetCachedSortName(B, DisplayResolver), ESearchCase::IgnoreCase);
}

//================================ UInvUI_SortByCount =================================

int32 UInvUI_SortByCount::CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	if (A.Count != B.Count)
	{
		return (A.Count < B.Count) ? -1 : 1;
	}
	// Same count: tie-break by name for a stable, meaningful order.
	return GetCachedSortName(A, DisplayResolver).Compare(
		GetCachedSortName(B, DisplayResolver), ESearchCase::IgnoreCase);
}

//================================ UInvUI_SortByType ==================================

int32 UInvUI_SortByType::BucketIndexFor(const FGameplayTag& Tag) const
{
	int32 Index = 0;
	for (const FGameplayTag& Category : TypeOrder)
	{
		if (Tag.MatchesTag(Category))
		{
			return Index;
		}
		++Index;
	}
	return TypeOrder.Num(); // unmatched -> trailing bucket
}

int32 UInvUI_SortByType::CompareSlots_Implementation(const FInvUI_SlotState& A, const FInvUI_SlotState& B,
	const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver) const
{
	const int32 BucketA = BucketIndexFor(A.ItemTag);
	const int32 BucketB = BucketIndexFor(B.ItemTag);
	if (BucketA != BucketB)
	{
		return (BucketA < BucketB) ? -1 : 1;
	}
	// Same category: order by name within the group.
	return GetCachedSortName(A, DisplayResolver).Compare(
		GetCachedSortName(B, DisplayResolver), ESearchCase::IgnoreCase);
}
