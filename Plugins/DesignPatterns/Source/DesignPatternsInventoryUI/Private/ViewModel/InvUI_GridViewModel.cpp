// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_GridViewModel.h"
#include "ViewModel/InvUI_SlotViewModel.h"
#include "Adapter/InvUI_ContainerAdapterObject.h"
#include "Core/DPLog.h"
#include "Async/Async.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UInvUI_GridViewModel's observable fields by name. */
	struct FInvUI_GridViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_GridViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_GridViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_GridViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_GridViewModelDescriptor::FieldNames[(int32)UInvUI_GridViewModel::EField::Num] =
	{
		FName(TEXT("ContainerId")),
		FName(TEXT("StructureRevision")),
		FName(TEXT("ColumnCount")),
		FName(TEXT("RowCount")),
		FName(TEXT("SlotCount")),
		FName(TEXT("Bound")),
	};

	static const FInvUI_GridViewModelDescriptor GInvUI_GridViewModelDescriptor;
}

UInvUI_GridViewModel::UInvUI_GridViewModel()
{
}

void UInvUI_GridViewModel::BeginDestroy()
{
	UnbindContainer();
	Super::BeginDestroy();
}

const UE::FieldNotification::IClassDescriptor& UInvUI_GridViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_GridViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_GridViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_GridViewModelDescriptor::MakeId(Field);
}

void UInvUI_GridViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

//==================================== Binding =======================================

void UInvUI_GridViewModel::BindContainer(const TScriptInterface<IInvUI_ItemContainer>& Container,
	UInvUI_ContainerAdapterObject* Adapter,
	const TScriptInterface<IInvUI_ItemDisplay>& InDisplayResolver)
{
	UnbindContainer();

	BoundContainer = Container;
	DisplayResolver = InDisplayResolver;

	UObject* ContainerObj = Container.GetObject();
	if (ContainerObj == nullptr || Container.GetInterface() == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] GridViewModel::BindContainer: null/invalid container."));
		return;
	}

	// Prefer the adapter's native (type-erased) change delegate; fall back to the seam dynamic one.
	if (Adapter != nullptr)
	{
		BoundAdapter = Adapter;
		Adapter->OnContainerChanged.AddUObject(this, &UInvUI_GridViewModel::HandleAdapterChanged);
	}
	else if (IInvUI_ItemContainer* SeamIface = Container.GetInterface())
	{
		FInvUI_OnContainerChangedDynamic& SeamDelegate = SeamIface->GetOnContainerChangedDelegate();
		SeamDelegate.AddDynamic(this, &UInvUI_GridViewModel::HandleSeamChanged);
	}

	if (!bBound)
	{
		bBound = true;
		BroadcastField(EField::bBound);
	}

	Rebuild();
}

void UInvUI_GridViewModel::UnbindContainer()
{
	// Unbind the adapter's native delegate.
	if (UInvUI_ContainerAdapterObject* Adapter = BoundAdapter.Get())
	{
		Adapter->OnContainerChanged.RemoveAll(this);
	}
	BoundAdapter.Reset();

	// Unbind the seam dynamic delegate (no-adapter path).
	if (UObject* ContainerObj = BoundContainer.GetObject())
	{
		if (IInvUI_ItemContainer* SeamIface = BoundContainer.GetInterface())
		{
			if (IsValid(ContainerObj))
			{
				FInvUI_OnContainerChangedDynamic& SeamDelegate = SeamIface->GetOnContainerChangedDelegate();
				SeamDelegate.RemoveDynamic(this, &UInvUI_GridViewModel::HandleSeamChanged);
			}
		}
	}

	// Cancel all in-flight icon streams.
	for (TPair<TWeakObjectPtr<UInvUI_SlotViewModel>, TSharedPtr<FStreamableHandle>>& Pair : ActiveStreams)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	ActiveStreams.Reset();

	BoundContainer = TScriptInterface<IInvUI_ItemContainer>();
	DisplayResolver = TScriptInterface<IInvUI_ItemDisplay>();

	const bool bHadSlots = ActiveSlotVMs.Num() > 0;
	ActiveSlotVMs.Reset();
	PoolCursor = 0;

	if (bBound)
	{
		bBound = false;
		BroadcastField(EField::bBound);
	}
	if (ContainerId.IsValid())
	{
		ContainerId = FInvUI_ContainerInstanceId::Invalid();
		BroadcastField(EField::ContainerId);
	}
	if (bHadSlots || ColumnCount != 0 || RowCount != 0)
	{
		ColumnCount = 0;
		RowCount = 0;
		++StructureRevision;
		BroadcastField(EField::ColumnCount);
		BroadcastField(EField::RowCount);
		BroadcastField(EField::SlotCount);
		BroadcastField(EField::StructureRevision);
	}
}

void UInvUI_GridViewModel::HandleAdapterChanged()
{
	Rebuild();
}

void UInvUI_GridViewModel::HandleSeamChanged(const FInvUI_ContainerInstanceId& /*InContainerId*/)
{
	Rebuild();
}

//==================================== Rebuild =======================================

UInvUI_SlotViewModel* UInvUI_GridViewModel::AcquireSlotVM()
{
	if (SlotVMPool.IsValidIndex(PoolCursor))
	{
		return SlotVMPool[PoolCursor++];
	}
	UInvUI_SlotViewModel* NewVM = NewObject<UInvUI_SlotViewModel>(this);
	SlotVMPool.Add(NewVM);
	++PoolCursor;
	return NewVM;
}

void UInvUI_GridViewModel::Rebuild()
{
	UObject* ContainerObj = BoundContainer.GetObject();
	if (ContainerObj == nullptr || BoundContainer.GetInterface() == nullptr)
	{
		// Nothing bound: ensure a clean empty state.
		if (ActiveSlotVMs.Num() > 0)
		{
			ActiveSlotVMs.Reset();
			++StructureRevision;
			BroadcastField(EField::StructureRevision);
			BroadcastField(EField::SlotCount);
		}
		return;
	}

	// 1) Refresh the bound container id.
	const FInvUI_ContainerInstanceId NewId =
		IInvUI_ItemContainer::Execute_GetContainerInstanceId(ContainerObj);
	if (NewId != ContainerId)
	{
		ContainerId = NewId;
		BroadcastField(EField::ContainerId);
	}

	// 2) Read all slots.
	TArray<FInvUI_SlotState> Slots;
	IInvUI_ItemContainer::Execute_GetSlots(ContainerObj, Slots);

	// 3) Filter.
	if (!ItemFilter.IsEmpty() || !bShowEmptySlots)
	{
		Slots.RemoveAll([this](const FInvUI_SlotState& S)
		{
			if (S.IsEmpty())
			{
				// Empty slots only survive when explicitly shown AND no filter is active.
				return !bShowEmptySlots || !ItemFilter.IsEmpty();
			}
			if (!ItemFilter.IsEmpty() && !S.ItemTag.MatchesAny(ItemFilter))
			{
				return true;
			}
			return false;
		});
	}

	// 4) Sort (display-only reorder).
	if (SortStrategy != nullptr)
	{
		SortStrategy->SortSlots(Slots, DisplayResolver);
	}

	// 5) Layout -> cell positions.
	FInvUI_LayoutResult Layout;
	if (LayoutStrategy != nullptr)
	{
		LayoutStrategy->BuildLayout(Slots, Layout);
	}
	else
	{
		// Single-row fallback.
		Layout.Positions.Reset(Slots.Num());
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			Layout.Positions.Emplace(Slots[i].SlotTag, i, 0);
		}
		Layout.ColumnCount = Slots.Num();
		Layout.RowCount = Slots.Num() > 0 ? 1 : 0;
	}

	// Map slot tag -> state for quick position pairing.
	TMap<FGameplayTag, const FInvUI_SlotState*> StateByTag;
	StateByTag.Reserve(Slots.Num());
	for (const FInvUI_SlotState& S : Slots)
	{
		StateByTag.Add(S.SlotTag, &S);
	}

	// 6) Diff-update the pooled slot viewmodels in layout order.
	const int32 PrevCount = ActiveSlotVMs.Num();
	TArray<FGameplayTag> PrevOrder;
	PrevOrder.Reserve(PrevCount);
	for (const TObjectPtr<UInvUI_SlotViewModel>& VM : ActiveSlotVMs)
	{
		PrevOrder.Add(VM ? VM->GetSlotTag() : FGameplayTag());
	}

	PoolCursor = 0;
	ActiveSlotVMs.Reset(Layout.Positions.Num());

	for (const FInvUI_SlotPosition& Pos : Layout.Positions)
	{
		if (!Pos.bValid)
		{
			continue;
		}
		const FInvUI_SlotState* const* StatePtr = StateByTag.Find(Pos.SlotTag);
		const FInvUI_SlotState State = StatePtr ? **StatePtr : FInvUI_SlotState();

		UInvUI_SlotViewModel* SlotVM = AcquireSlotVM();

		const FGameplayTag PrevItem = SlotVM->GetItemTag();
		const bool bStateChanged = SlotVM->SetSlotState(State);
		SlotVM->SetCellPosition(Pos.Column, Pos.Row);

		// (Re)resolve display if the item identity changed or the vm has no icon yet.
		if (State.IsOccupied() && (PrevItem != State.ItemTag || SlotVM->GetIcon() == nullptr))
		{
			CancelStreamForSlot(SlotVM);
			RequestDisplayForSlot(SlotVM, State.ItemTag);
		}
		else if (State.IsEmpty())
		{
			CancelStreamForSlot(SlotVM);
		}

		ActiveSlotVMs.Add(SlotVM);
		(void)bStateChanged;
	}

	// 7) Update coarse layout fields.
	if (ColumnCount != Layout.ColumnCount)
	{
		ColumnCount = Layout.ColumnCount;
		BroadcastField(EField::ColumnCount);
	}
	if (RowCount != Layout.RowCount)
	{
		RowCount = Layout.RowCount;
		BroadcastField(EField::RowCount);
	}

	// 8) Bump StructureRevision only when the slot SET or order actually changed.
	bool bStructureChanged = (ActiveSlotVMs.Num() != PrevCount);
	if (!bStructureChanged)
	{
		for (int32 i = 0; i < ActiveSlotVMs.Num(); ++i)
		{
			const FGameplayTag NowTag = ActiveSlotVMs[i] ? ActiveSlotVMs[i]->GetSlotTag() : FGameplayTag();
			if (!PrevOrder.IsValidIndex(i) || PrevOrder[i] != NowTag)
			{
				bStructureChanged = true;
				break;
			}
		}
	}
	if (bStructureChanged)
	{
		++StructureRevision;
		BroadcastField(EField::StructureRevision);
		BroadcastField(EField::SlotCount);
	}
}

//================================ Display / icons ===================================

void UInvUI_GridViewModel::RequestDisplayForSlot(UInvUI_SlotViewModel* SlotVM, const FGameplayTag& ItemTag)
{
	if (SlotVM == nullptr || !ItemTag.IsValid())
	{
		return;
	}

	UObject* ResolverObj = DisplayResolver.GetObject();
	if (ResolverObj == nullptr || DisplayResolver.GetInterface() == nullptr)
	{
		// No resolver: leave the slot showing its tag-derived name and no icon.
		return;
	}

	// Fast path: already-cached info -> apply synchronously and stream the icon.
	FInvUI_ItemDisplayInfo Cached;
	if (IInvUI_ItemDisplay::Execute_TryGetCachedDisplay(ResolverObj, ItemTag, Cached) && Cached.bResolved)
	{
		SlotVM->ApplyDisplayInfo(Cached.DisplayName, Cached.Description, nullptr, Cached.QualityColor);
		StreamIconForSlot(SlotVM, ItemTag, Cached.Icon, Cached.DisplayName, Cached.Description, Cached.QualityColor);
		return;
	}

	// Async path: bind a delegate that applies the result if the slot still shows this item.
	TWeakObjectPtr<UInvUI_SlotViewModel> WeakSlot(SlotVM);
	TWeakObjectPtr<UInvUI_GridViewModel> WeakSelf(this);
	const FGameplayTag CapturedTag = ItemTag;

	FInvUI_OnItemDisplayResolved OnResolved;
	OnResolved.BindWeakLambda(this, [WeakSelf, WeakSlot, CapturedTag](const FInvUI_ItemDisplayInfo& Info)
	{
		UInvUI_GridViewModel* Self = WeakSelf.Get();
		UInvUI_SlotViewModel* Slot = WeakSlot.Get();
		if (Self == nullptr || Slot == nullptr)
		{
			return;
		}
		// Ignore a stale resolution for a slot now showing a different item.
		if (Slot->GetItemTag() != CapturedTag || !Info.bResolved)
		{
			return;
		}
		Slot->ApplyDisplayInfo(Info.DisplayName, Info.Description, nullptr, Info.QualityColor);
		Self->StreamIconForSlot(Slot, CapturedTag, Info.Icon, Info.DisplayName, Info.Description, Info.QualityColor);
	});

	IInvUI_ItemDisplay::Execute_ResolveItemDisplay(ResolverObj, ItemTag, OnResolved);
}

void UInvUI_GridViewModel::StreamIconForSlot(UInvUI_SlotViewModel* SlotVM, const FGameplayTag& ItemTag,
	const TSoftObjectPtr<UTexture2D>& SoftIcon, const FText& Name, const FText& Desc, const FLinearColor& Quality)
{
	if (SlotVM == nullptr)
	{
		return;
	}

	if (SoftIcon.IsNull())
	{
		// No icon to stream; the textual info was already applied.
		return;
	}

	// Already resident? Apply immediately.
	if (UTexture2D* Loaded = SoftIcon.Get())
	{
		SlotVM->ApplyDisplayInfo(Name, Desc, Loaded, Quality);
		return;
	}

	CancelStreamForSlot(SlotVM);

	TWeakObjectPtr<UInvUI_SlotViewModel> WeakSlot(SlotVM);
	TWeakObjectPtr<UInvUI_GridViewModel> WeakSelf(this);
	const FGameplayTag CapturedTag = ItemTag;
	const FSoftObjectPath IconPath = SoftIcon.ToSoftObjectPath();

	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(IconPath,
		FStreamableDelegate::CreateWeakLambda(this,
			[WeakSelf, WeakSlot, CapturedTag, IconPath, Name, Desc, Quality]()
		{
			UInvUI_GridViewModel* Self = WeakSelf.Get();
			UInvUI_SlotViewModel* Slot = WeakSlot.Get();
			if (Self != nullptr)
			{
				Self->ActiveStreams.Remove(WeakSlot);
			}
			if (Slot == nullptr || Slot->GetItemTag() != CapturedTag)
			{
				return; // slot reused for a different item; drop the load
			}
			UTexture2D* Texture = Cast<UTexture2D>(IconPath.ResolveObject());
			if (Texture != nullptr)
			{
				// RequestAsyncLoad's completion delegate may fire on a loader thread; ApplyDisplayInfo
				// mutates UPROPERTYs and broadcasts field-changed, which is game-thread-only. Marshal it
				// (capturing the slot weakly by value, never raw this). Mirrors SaveX_StorageSubsystem.
				AsyncTask(ENamedThreads::GameThread, [WeakSlot, CapturedTag, Name, Desc, Texture, Quality]()
				{
					UInvUI_SlotViewModel* GameThreadSlot = WeakSlot.Get();
					if (GameThreadSlot != nullptr && GameThreadSlot->GetItemTag() == CapturedTag)
					{
						GameThreadSlot->ApplyDisplayInfo(Name, Desc, Texture, Quality);
					}
				});
			}
		}));

	if (Handle.IsValid())
	{
		ActiveStreams.Add(WeakSlot, Handle);
	}
}

void UInvUI_GridViewModel::CancelStreamForSlot(const UInvUI_SlotViewModel* SlotVM)
{
	if (SlotVM == nullptr)
	{
		return;
	}
	const TWeakObjectPtr<UInvUI_SlotViewModel> Key(const_cast<UInvUI_SlotViewModel*>(SlotVM));
	if (TSharedPtr<FStreamableHandle>* HandlePtr = ActiveStreams.Find(Key))
	{
		if (HandlePtr->IsValid())
		{
			(*HandlePtr)->CancelHandle();
		}
		ActiveStreams.Remove(Key);
	}
}

//==================================== Tuning ========================================

void UInvUI_GridViewModel::SetSortStrategy(UInvUI_SortStrategy* InStrategy)
{
	SortStrategy = InStrategy;
	Rebuild();
}

void UInvUI_GridViewModel::SetLayoutStrategy(UInvUI_LayoutStrategy* InStrategy)
{
	LayoutStrategy = InStrategy;
	Rebuild();
}

void UInvUI_GridViewModel::SetItemFilter(const FGameplayTagContainer& InFilter)
{
	ItemFilter = InFilter;
	Rebuild();
}

void UInvUI_GridViewModel::SetShowEmptySlots(bool bInShow)
{
	if (bShowEmptySlots != bInShow)
	{
		bShowEmptySlots = bInShow;
		Rebuild();
	}
}

TArray<UInvUI_SlotViewModel*> UInvUI_GridViewModel::GetSlotViewModels() const
{
	TArray<UInvUI_SlotViewModel*> Result;
	Result.Reserve(ActiveSlotVMs.Num());
	for (const TObjectPtr<UInvUI_SlotViewModel>& VM : ActiveSlotVMs)
	{
		Result.Add(VM.Get());
	}
	return Result;
}

UInvUI_SlotViewModel* UInvUI_GridViewModel::FindSlotViewModel(FGameplayTag SlotTag) const
{
	for (const TObjectPtr<UInvUI_SlotViewModel>& VM : ActiveSlotVMs)
	{
		if (VM && VM->GetSlotTag() == SlotTag)
		{
			return VM;
		}
	}
	return nullptr;
}
