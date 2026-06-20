// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "GameplayTagContainer.h"

// Stable container identity originates in the InventoryUI seam layer (FInvUI_ContainerInstanceId).
// A drag operation carries IDENTITY only — never a widget/index.
#include "Seam/InvUI_ItemContainer.h"

#include "InvUI_DragDropOperation.generated.h"

/**
 * Drag-and-drop payload for inventory slot drags.
 *
 * CRITICAL DESIGN RULE: this operation carries ONLY stable identity — the source
 * container instance id, the source slot tag, the dragged item's identity tag and a
 * count. It deliberately holds NO raw widget pointer, NO array index and NO backend
 * container pointer. A widget can be rebuilt/recycled mid-drag (the grid refreshes on
 * replication), and an index can shift when stacks merge/split; identity cannot. The
 * drop target therefore asks the per-player mediator to perform the move BY IDENTITY,
 * and the server independently re-resolves both containers from the registry. Nothing
 * the client names is trusted as a pointer.
 *
 * The operation is a pure data carrier; the visual "drag cursor" is the standard
 * UDragDropOperation::DefaultDragVisual (a widget the source slot builds and assigns).
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Drag Drop Operation"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_DragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** Stable id of the container the drag STARTED from. Re-resolved server-side; never trusted as a pointer. */
	UPROPERTY(BlueprintReadWrite, Category = "InvUI|DragDrop")
	FInvUI_ContainerInstanceId SourceContainerId;

	/** Slot the drag started from, addressed by its stable slot tag (grid cell, equip slot, etc.). */
	UPROPERTY(BlueprintReadWrite, Category = "InvUI|DragDrop")
	FGameplayTag SourceSlotTag;

	/** Identity tag of the item being dragged (matches the backend item definition's data tag). */
	UPROPERTY(BlueprintReadWrite, Category = "InvUI|DragDrop")
	FGameplayTag ItemTag;

	/**
	 * Number of units this drag represents. A whole-stack drag carries the full count; a
	 * split affordance carries a partial count. The server clamps to what actually exists.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "InvUI|DragDrop")
	int32 Count = 0;

	/**
	 * Construct (or reuse) a drag operation describing a slot drag by identity.
	 *
	 * Static factory so source slot widgets build the payload in one call without touching
	 * UMG internals. The returned operation has no default visual assigned — the caller may
	 * set DefaultDragVisual / Pivot / Offset afterwards.
	 *
	 * @param InSourceContainerId  Stable id of the source container.
	 * @param InSourceSlotTag      Stable tag of the source slot.
	 * @param InItemTag            Identity tag of the dragged item.
	 * @param InCount              Units being dragged (>= 1 for a meaningful drag).
	 * @return A fully-populated, identity-only drag operation.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|DragDrop", meta = (DisplayName = "Make Slot Drag Operation"))
	static UInvUI_DragDropOperation* MakeSlotDrag(
		FInvUI_ContainerInstanceId InSourceContainerId,
		FGameplayTag InSourceSlotTag,
		FGameplayTag InItemTag,
		int32 InCount);

	/** True when this payload describes a non-empty, well-formed drag (valid source id, slot, item, count >= 1). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|DragDrop")
	bool IsValidDrag() const;

	/** Human-readable description for logging/tooltips. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|DragDrop")
	FString ToDebugString() const;
};
