// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_SlotContextMenuWidget.h"
#include "Widget/InvUI_SlotWidget.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"
#include "Intent/InvUI_SpatialIntentComponent.h"
#include "Registry/InvUI_ContainerRegistry.h"
#include "Seam/InvUI_ContainerCapabilities.h"
#include "InvUI_NativeTags.h"
#include "Core/DPLog.h"

void UInvUI_SlotContextMenuWidget::SetRouters(UInvUI_ContainerMediatorComponent* InMediator,
	UInvUI_SpatialIntentComponent* InSpatialIntent)
{
	Mediator = InMediator;
	SpatialIntent = InSpatialIntent;
}

TScriptInterface<IInvUI_ItemContainer> UInvUI_SlotContextMenuWidget::ResolveBoundContainer() const
{
	if (UInvUI_ContainerRegistry* Registry = UInvUI_ContainerRegistry::Get(this))
	{
		return Registry->ResolveContainer(BoundContainerId);
	}
	return TScriptInterface<IInvUI_ItemContainer>();
}

void UInvUI_SlotContextMenuWidget::OpenForSlot(UInvUI_SlotWidget* SlotWidget, FInvUI_ContainerInstanceId QuickMoveTarget)
{
	BoundSlot = SlotWidget;
	QuickMoveTargetId = QuickMoveTarget;
	Capabilities.Reset();

	if (SlotWidget == nullptr)
	{
		OnBuildActions(Capabilities);
		return;
	}

	BoundContainerId = SlotWidget->GetContainerId();
	BoundSlotTag = SlotWidget->GetSlotTag();

	// Resolve the container's advertised capabilities through the optional seam (default: none).
	TScriptInterface<IInvUI_ItemContainer> Container = ResolveBoundContainer();
	if (UObject* Obj = Container.GetObject())
	{
		if (Obj->GetClass()->ImplementsInterface(UInvUI_ContainerCapabilities::StaticClass()))
		{
			IInvUI_ContainerCapabilities::Execute_GetCapabilities(Obj, Capabilities);
		}
	}

	OnBuildActions(Capabilities);
}

void UInvUI_SlotContextMenuWidget::InvokeAction(FGameplayTag IntentTag)
{
	if (!BoundSlotTag.IsValid() || !BoundContainerId.IsValid())
	{
		return;
	}

	// Rotate routes to the spatial intent component.
	if (IntentTag == InvUITags::Intent_Rotate)
	{
		if (UInvUI_SpatialIntentComponent* Intent = SpatialIntent.Get())
		{
			Intent->RequestRotate(BoundContainerId, BoundSlotTag);
		}
		return;
	}

	// All other verbs are expressed through the move mediator as identity moves. The destination
	// container/slot are chosen by intent: Use/Drop act in place (same container, empty slot), Equip
	// and quick-move target the QuickMoveTarget when one is set.
	UInvUI_ContainerMediatorComponent* Med = Mediator.Get();
	if (Med == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("UInvUI_SlotContextMenuWidget::InvokeAction: no mediator; ignored (%s)."),
			*IntentTag.ToString());
		return;
	}

	FInvUI_ContainerInstanceId ToContainer = BoundContainerId;
	if ((IntentTag == InvUITags::Intent_Equip || IntentTag == InvUITags::Intent_Use) && QuickMoveTargetId.IsValid())
	{
		ToContainer = QuickMoveTargetId;
	}

	// Whole-stack move (Count 0). Split is driven by the dedicated splitter dialog, not this verb;
	// if a project wires Intent_Split here it still routes as a whole-stack move and the backend can
	// reinterpret it — but the canonical split path is the quantity splitter VM.
	Med->RequestMoveByIdentity(BoundContainerId, BoundSlotTag, ToContainer, FGameplayTag(), 0);
}

void UInvUI_SlotContextMenuWidget::QuickMove()
{
	if (!BoundSlotTag.IsValid() || !BoundContainerId.IsValid() || !QuickMoveTargetId.IsValid())
	{
		return;
	}
	if (UInvUI_ContainerMediatorComponent* Med = Mediator.Get())
	{
		// Move the whole stack to the quick-move target; the server chooses the destination slot.
		Med->RequestMoveByIdentity(BoundContainerId, BoundSlotTag, QuickMoveTargetId, FGameplayTag(), 0);
	}
}
