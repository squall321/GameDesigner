// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_DragDropOperation.h"
#include "Core/DPLog.h"

UInvUI_DragDropOperation* UInvUI_DragDropOperation::MakeSlotDrag(
	FInvUI_ContainerInstanceId InSourceContainerId,
	FGameplayTag InSourceSlotTag,
	FGameplayTag InItemTag,
	int32 InCount)
{
	// Transient operation owned by the UMG drag-drop machinery for the duration of the drag.
	UInvUI_DragDropOperation* Op = NewObject<UInvUI_DragDropOperation>();
	Op->SourceContainerId = InSourceContainerId;
	Op->SourceSlotTag = InSourceSlotTag;
	Op->ItemTag = InItemTag;
	Op->Count = FMath::Max(InCount, 0);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Built slot drag: %s"), *Op->ToDebugString());
	return Op;
}

bool UInvUI_DragDropOperation::IsValidDrag() const
{
	return SourceContainerId.IsValid()
		&& SourceSlotTag.IsValid()
		&& ItemTag.IsValid()
		&& Count >= 1;
}

FString UInvUI_DragDropOperation::ToDebugString() const
{
	return FString::Printf(
		TEXT("InvUI_Drag[Item=%s x%d From=%s::%s]"),
		*ItemTag.ToString(),
		Count,
		*SourceContainerId.ToString(),
		*SourceSlotTag.ToString());
}
