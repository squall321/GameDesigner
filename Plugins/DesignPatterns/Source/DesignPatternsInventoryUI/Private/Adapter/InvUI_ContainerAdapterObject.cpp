// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Adapter/InvUI_ContainerAdapterObject.h"
#include "Core/DPLog.h"

void UInvUI_ContainerAdapterObject::InitializeAdapter(
	UObject* InBackend, const TScriptInterface<IInvUI_ItemContainer>& Container)
{
	// Tear down any prior binding so re-initialization is safe.
	ShutdownAdapter();

	TargetContainer = Container;

	if (InBackend == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] InitializeAdapter called with a null backend on '%s'."),
			*GetNameSafe(this));
		return;
	}

	BoundBackend = InBackend;

	// Auto-bind the seam dynamic delegate when the backend (or its forwarding target) exposes
	// one, so simple backends need no concrete adapter override at all. The seam delegate is a
	// native (non-reflected) virtual, so call it directly through the interface pointer.
	if (IInvUI_ItemContainer* SeamIface = TargetContainer.GetInterface())
	{
		FInvUI_OnContainerChangedDynamic& SeamDelegate = SeamIface->GetOnContainerChangedDelegate();
		SeamDelegate.AddDynamic(this, &UInvUI_ContainerAdapterObject::HandleSeamContainerChanged);
	}

	// Let a concrete adapter wire up the backend's own (non-seam) dynamic delegate.
	BindBackend(InBackend);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Adapter '%s' bound to backend '%s'."),
		*GetNameSafe(this), *GetNameSafe(InBackend));
}

void UInvUI_ContainerAdapterObject::ShutdownAdapter()
{
	UObject* Backend = BoundBackend.Get();

	// Undo the auto-bound seam delegate.
	if (UObject* SeamObj = TargetContainer.GetObject())
	{
		if (IInvUI_ItemContainer* SeamIface = TargetContainer.GetInterface())
		{
			if (IsValid(SeamObj))
			{
				FInvUI_OnContainerChangedDynamic& SeamDelegate = SeamIface->GetOnContainerChangedDelegate();
				SeamDelegate.RemoveDynamic(this, &UInvUI_ContainerAdapterObject::HandleSeamContainerChanged);
			}
		}
	}

	if (Backend != nullptr)
	{
		UnbindBackend(Backend);
	}

	BoundBackend.Reset();
	TargetContainer = TScriptInterface<IInvUI_ItemContainer>();
}

void UInvUI_ContainerAdapterObject::BeginDestroy()
{
	ShutdownAdapter();
	Super::BeginDestroy();
}

void UInvUI_ContainerAdapterObject::BindBackend(UObject* /*InBackend*/)
{
	// Base: nothing extra. Concrete adapters override to bind a backend-specific delegate.
}

void UInvUI_ContainerAdapterObject::UnbindBackend(UObject* /*InBackend*/)
{
	// Base: nothing extra. Concrete adapters override to unbind their backend-specific delegate.
}

void UInvUI_ContainerAdapterObject::NotifyContainerChanged()
{
	OnContainerChanged.Broadcast();
}

void UInvUI_ContainerAdapterObject::HandleSeamContainerChanged(const FInvUI_ContainerInstanceId& /*ContainerId*/)
{
	NotifyContainerChanged();
}

FInvUI_ContainerInstanceId UInvUI_ContainerAdapterObject::GetContainerInstanceId_Implementation() const
{
	if (UObject* Obj = TargetContainer.GetObject())
	{
		if (TargetContainer.GetInterface() != nullptr)
		{
			return IInvUI_ItemContainer::Execute_GetContainerInstanceId(Obj);
		}
	}
	return FInvUI_ContainerInstanceId::Invalid();
}

void UInvUI_ContainerAdapterObject::GetSlots_Implementation(TArray<FInvUI_SlotState>& OutSlots) const
{
	OutSlots.Reset();
	if (UObject* Obj = TargetContainer.GetObject())
	{
		if (TargetContainer.GetInterface() != nullptr)
		{
			IInvUI_ItemContainer::Execute_GetSlots(Obj, OutSlots);
		}
	}
}

bool UInvUI_ContainerAdapterObject::GetSlot_Implementation(FGameplayTag SlotTag, FInvUI_SlotState& OutSlot) const
{
	if (UObject* Obj = TargetContainer.GetObject())
	{
		if (TargetContainer.GetInterface() != nullptr)
		{
			return IInvUI_ItemContainer::Execute_GetSlot(Obj, SlotTag, OutSlot);
		}
	}
	OutSlot = FInvUI_SlotState();
	return false;
}

int32 UInvUI_ContainerAdapterObject::GetCapacity_Implementation() const
{
	if (UObject* Obj = TargetContainer.GetObject())
	{
		if (TargetContainer.GetInterface() != nullptr)
		{
			return IInvUI_ItemContainer::Execute_GetCapacity(Obj);
		}
	}
	return 0;
}

bool UInvUI_ContainerAdapterObject::QueryCanAccept_Implementation(
	const FInvUI_SlotState& Candidate, FGameplayTag SlotTag) const
{
	if (UObject* Obj = TargetContainer.GetObject())
	{
		if (TargetContainer.GetInterface() != nullptr)
		{
			return IInvUI_ItemContainer::Execute_QueryCanAccept(Obj, Candidate, SlotTag);
		}
	}
	return false;
}

FInvUI_OnContainerChangedDynamic& UInvUI_ContainerAdapterObject::GetOnContainerChangedDelegate()
{
	// The adapter does not expose a dynamic seam delegate of its own; viewmodels bind to the
	// native OnContainerChanged instead. Return a stable empty delegate so callers that go
	// through the seam still get a valid reference.
	static FInvUI_OnContainerChangedDynamic Empty;
	return Empty;
}
