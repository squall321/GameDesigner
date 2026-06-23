// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DragDrop/DPDragDropOperation.h"
#include "DPUINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Blueprint/UserWidget.h"

UDP_DragDropOperationBase* UDP_DragDropOperationBase::MakeDrag(FGameplayTag PayloadType, FInstancedStruct Payload,
	UUserWidget* Visual, UWidget* Source)
{
	UDP_DragDropOperationBase* Op = NewObject<UDP_DragDropOperationBase>();
	Op->DragPayload.PayloadType = PayloadType;
	Op->DragPayload.Payload = MoveTemp(Payload);
	Op->DragPayload.SourceWidget = Source;

	// Wire the standard UMG drag visual so the base pipeline renders it under the cursor.
	if (Visual)
	{
		Op->DefaultDragVisual = Visual;
		Op->Pivot = EDragPivot::MouseDown;
	}

	return Op;
}

void UDP_DragDropOperationBase::NotifyDropResult(bool bAccepted, FGameplayTag DropZoneTag)
{
	// Resolve the bus from the source widget's world context (the operation has no world of its own).
	const UObject* WorldContext = DragPayload.SourceWidget.Get();
	if (!WorldContext)
	{
		WorldContext = GetOuter();
	}

	UDP_MessageBusSubsystem* Bus = WorldContext
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(WorldContext)
		: nullptr;
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[DragDrop] NotifyDropResult: bus unavailable; dropping result for %s."),
			*DragPayload.PayloadType.ToString());
		return;
	}

	FDP_DragDropResultPayload Result;
	Result.Payload = DragPayload;
	Result.bAccepted = bAccepted;
	Result.DropZoneTag = DropZoneTag;

	Bus->BroadcastPayload(DPUITags::Bus_DragDropCompleted,
		FInstancedStruct::Make(Result), const_cast<UObject*>(WorldContext));
}
