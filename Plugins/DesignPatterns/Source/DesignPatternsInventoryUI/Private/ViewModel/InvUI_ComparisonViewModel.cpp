// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_ComparisonViewModel.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Hand-rolled descriptor enumerating UInvUI_ComparisonViewModel's observable fields. */
	struct FInvUI_ComparisonViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_ComparisonViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_ComparisonViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_ComparisonViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_ComparisonViewModelDescriptor::FieldNames[(int32)UInvUI_ComparisonViewModel::EField::Num] =
	{
		FName(TEXT("HoveredItem")),
		FName(TEXT("EquippedItem")),
		FName(TEXT("DeltaRows")),
		FName(TEXT("bHasComparison")),
	};

	static const FInvUI_ComparisonViewModelDescriptor GInvUI_ComparisonViewModelDescriptor;
}

UInvUI_ComparisonViewModel::UInvUI_ComparisonViewModel()
{
}

const UE::FieldNotification::IClassDescriptor& UInvUI_ComparisonViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_ComparisonViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_ComparisonViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_ComparisonViewModelDescriptor::MakeId(Field);
}

void UInvUI_ComparisonViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UInvUI_ComparisonViewModel::SetStatResolver(const TScriptInterface<ISeam_ItemStats>& InResolver)
{
	StatResolver = InResolver;
}

FGameplayTag UInvUI_ComparisonViewModel::ReadItemTag(
	const TScriptInterface<IInvUI_ItemContainer>& Container, const FGameplayTag& SlotTag)
{
	if (Container.GetObject() == nullptr || !SlotTag.IsValid())
	{
		return FGameplayTag();
	}
	FInvUI_SlotState State;
	if (IInvUI_ItemContainer::Execute_GetSlot(Container.GetObject(), SlotTag, State) && State.IsOccupied())
	{
		return State.ItemTag;
	}
	return FGameplayTag();
}

void UInvUI_ComparisonViewModel::SetHovered(
	const TScriptInterface<IInvUI_ItemContainer>& Container, FGameplayTag SlotTag)
{
	const FGameplayTag NewItem = ReadItemTag(Container, SlotTag);
	if (SetProperty(GetFieldId(EField::HoveredItem), HoveredItem, NewItem))
	{
		bHoveredResolved = false;
		HoveredStats = FSeam_ItemStatSet();
	}
	ResolveSide(HoveredItem, /*bHoveredSide*/ true);
	RebuildDeltas();
}

void UInvUI_ComparisonViewModel::SetEquippedContainer(
	const TScriptInterface<IInvUI_ItemContainer>& Container, FGameplayTag EquipSlot)
{
	const FGameplayTag NewItem = ReadItemTag(Container, EquipSlot);
	if (SetProperty(GetFieldId(EField::EquippedItem), EquippedItem, NewItem))
	{
		bEquippedResolved = false;
		EquippedStats = FSeam_ItemStatSet();
	}
	ResolveSide(EquippedItem, /*bHoveredSide*/ false);
	RebuildDeltas();
}

void UInvUI_ComparisonViewModel::Clear()
{
	SetProperty(GetFieldId(EField::HoveredItem), HoveredItem, FGameplayTag());
	SetProperty(GetFieldId(EField::EquippedItem), EquippedItem, FGameplayTag());
	HoveredStats = FSeam_ItemStatSet();
	EquippedStats = FSeam_ItemStatSet();
	bHoveredResolved = false;
	bEquippedResolved = false;

	if (DeltaRows.Num() > 0)
	{
		DeltaRows.Reset();
		BroadcastField(EField::DeltaRows);
	}
	SetProperty(GetFieldId(EField::bHasComparison), bHasComparison, false);
}

void UInvUI_ComparisonViewModel::ResolveSide(const FGameplayTag& ItemTag, bool bHoveredSide)
{
	bool& bResolvedRef = bHoveredSide ? bHoveredResolved : bEquippedResolved;
	FSeam_ItemStatSet& SetRef = bHoveredSide ? HoveredStats : EquippedStats;

	if (!ItemTag.IsValid())
	{
		// Empty side -> resolved with no mods (so an "equip into an empty slot" still diffs).
		SetRef = FSeam_ItemStatSet();
		SetRef.bResolved = true;
		bResolvedRef = true;
		return;
	}

	if (StatResolver.GetObject() == nullptr)
	{
		// No resolver -> treat as resolved-empty so the UI shows the item with no deltas.
		SetRef = FSeam_ItemStatSet();
		SetRef.ItemTag = ItemTag;
		SetRef.bResolved = false;
		bResolvedRef = true;
		return;
	}

	// Synchronous fast path.
	FSeam_ItemStatSet Cached;
	if (ISeam_ItemStats::Execute_TryGetItemStats(StatResolver.GetObject(), ItemTag, Cached))
	{
		SetRef = Cached;
		bResolvedRef = true;
		return;
	}

	// Async path: bind the appropriate sink and wait for the callback.
	bResolvedRef = false;
	FSeam_OnItemStatsResolved OnResolved;
	if (bHoveredSide)
	{
		OnResolved.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UInvUI_ComparisonViewModel, OnHoveredStatsResolved));
	}
	else
	{
		OnResolved.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UInvUI_ComparisonViewModel, OnEquippedStatsResolved));
	}
	ISeam_ItemStats::Execute_ResolveItemStats(StatResolver.GetObject(), ItemTag, OnResolved);
}

void UInvUI_ComparisonViewModel::OnHoveredStatsResolved(const FSeam_ItemStatSet& Stats)
{
	// Ignore a stale callback for an item we no longer hover.
	if (Stats.ItemTag != HoveredItem && HoveredItem.IsValid())
	{
		return;
	}
	HoveredStats = Stats;
	bHoveredResolved = true;
	RebuildDeltas();
}

void UInvUI_ComparisonViewModel::OnEquippedStatsResolved(const FSeam_ItemStatSet& Stats)
{
	if (Stats.ItemTag != EquippedItem && EquippedItem.IsValid())
	{
		return;
	}
	EquippedStats = Stats;
	bEquippedResolved = true;
	RebuildDeltas();
}

void UInvUI_ComparisonViewModel::RebuildDeltas()
{
	if (!bHoveredResolved || !bEquippedResolved)
	{
		// Wait until both sides have resolved (sync or async) before publishing rows.
		return;
	}

	// Gather the union of attributes touched by either side, preserving first-seen order.
	TArray<FGameplayTag> Attributes;
	auto AddAttrs = [&Attributes](const FSeam_ItemStatSet& Set)
	{
		for (const FSeam_StatMod& Mod : Set.Mods)
		{
			if (Mod.AttributeTag.IsValid())
			{
				Attributes.AddUnique(Mod.AttributeTag);
			}
		}
	};
	AddAttrs(HoveredStats);
	AddAttrs(EquippedStats);

	TArray<FInvUI_StatDelta> NewRows;
	NewRows.Reserve(Attributes.Num());
	for (const FGameplayTag& Attr : Attributes)
	{
		FInvUI_StatDelta Row;
		Row.Attribute = Attr;
		Row.Hovered = (float)HoveredStats.GetAttributeTotal(Attr);
		Row.Equipped = (float)EquippedStats.GetAttributeTotal(Attr);
		Row.Delta = Row.Hovered - Row.Equipped;
		Row.bIsUpgrade = Row.Delta > 0.f;
		NewRows.Add(Row);
	}

	DeltaRows = MoveTemp(NewRows);
	BroadcastField(EField::DeltaRows);

	SetProperty(GetFieldId(EField::bHasComparison), bHasComparison, true);
}
