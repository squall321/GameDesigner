// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPDragDropOperation.generated.h"

class UUserWidget;
class UWidget;

/**
 * The payload carried by a drag operation.
 *
 * It is deliberately generic: a tagged, CLIENT-LOCAL FInstancedStruct plus the source widget. The
 * same framework therefore serves inventory items, loadout slots, skill-tree nodes, window docking,
 * etc. — the payload TYPE tag tells drop zones whether they can accept it, and the typed struct
 * carries whatever the producer needs.
 *
 * IMPORTANT: this is purely client-side presentation. The FInstancedStruct is NEVER replicated.
 * Any authoritative mutation that a drop should cause is expressed as a tagged INTENT published on
 * the core message bus (UDP_ViewBase::PublishIntent) for the mediator/game systems to validate and
 * route — drag-drop never issues a Server RPC from the UI.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSUI_API FDP_DragPayload
{
	GENERATED_BODY()

	/** Identity of what is being dragged (e.g. DP.UI.Drag.InventoryItem). Drop zones gate on this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	FGameplayTag PayloadType;

	/** The typed, client-local payload. Make with the engine's Make Instanced Struct node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	FInstancedStruct Payload;

	/** The widget the drag originated from (weak — purely informational for the drop side). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	TWeakObjectPtr<UWidget> SourceWidget;
};

/**
 * The result of a completed (accepted or rejected) drag, published on DPUITags::Bus_DragDropCompleted
 * so source and target stay decoupled — neither calls the other directly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSUI_API FDP_DragDropResultPayload
{
	GENERATED_BODY()

	/** The payload that was dropped. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	FDP_DragPayload Payload;

	/** True if a drop zone accepted the payload. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	bool bAccepted = false;

	/** Identity tag of the accepting drop zone, if any (zone-supplied). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|UI|DragDrop")
	FGameplayTag DropZoneTag;
};

/**
 * Generic drag operation built on UMG's UDragDropOperation. Construct with MakeDrag, set a drag
 * visual, and the standard UMG drag pipeline carries it. Reusable across every UI domain.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_DragDropOperationBase : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/**
	 * Build a drag operation with a tagged payload and an optional drag-visual widget.
	 *
	 * @param PayloadType  Identity tag drop zones gate on.
	 * @param Payload      Typed client-local payload (never replicated).
	 * @param Visual       Optional widget shown under the cursor while dragging.
	 * @param Source       The widget the drag originated from.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|DragDrop", meta = (AdvancedDisplay = "Visual,Source"))
	static UDP_DragDropOperationBase* MakeDrag(FGameplayTag PayloadType, FInstancedStruct Payload,
		UUserWidget* Visual = nullptr, UWidget* Source = nullptr);

	/** The full payload. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|DragDrop")
	const FDP_DragPayload& GetPayload() const { return DragPayload; }

	/** The payload-type tag (convenience). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|DragDrop")
	FGameplayTag GetPayloadType() const { return DragPayload.PayloadType; }

	/**
	 * Publish the drop result on the core bus so the source/target/game systems can react without
	 * coupling. Called by IDP_DropZone implementations (or the drop handler) after a drop decision.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|DragDrop")
	void NotifyDropResult(bool bAccepted, FGameplayTag DropZoneTag);

protected:
	/** The carried payload. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|UI|DragDrop")
	FDP_DragPayload DragPayload;
};
