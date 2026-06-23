// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_QuantitySplitterViewModel.generated.h"

class UInvUI_ContainerMediatorComponent;

/**
 * Backs the quantity-splitter dialog (a slider/spinbox the player uses to pick how many units of a
 * stack to move out). Begin() seeds the source (container, slot, stack count); Selected is clamped
 * to [Min, Max]. ConfirmSplit routes through the EXISTING server-validated mediator
 * (UInvUI_ContainerMediatorComponent::RequestMoveByIdentity) with the partial Count and an empty
 * ToSlot, so the backend places the split — a split is just the shipped move primitive, NO new RPC.
 * Hand-rolled FieldNotification; holds only identities, never a backend pointer.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Quantity Splitter ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_QuantitySplitterViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_QuantitySplitterViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		Selected = 0,
		Min,
		Max,
		SlotTag,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Begin a split session for SlotTag in SourceContainer holding StackCount units. Sets Min=1,
	 * Max=StackCount-1 (you cannot split off the whole stack — that is a plain move), Selected=Max/2
	 * rounded up. A StackCount <= 1 yields Max=0 (the dialog should not open).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void Begin(FInvUI_ContainerInstanceId SourceContainer, FGameplayTag SlotTag, int32 StackCount);

	/** Set the selected split count, clamped to [Min, Max]. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void SetSelected(int32 InSelected);

	/** Nudge Selected by Delta (e.g. +/- buttons), clamped. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void StepSelected(int32 Delta);

	/**
	 * Confirm the split: ask the mediator to move Selected units from the source slot into
	 * ToContainer with an empty ToSlot (the backend chooses the destination slot / new stack).
	 * No-op when the session is invalid or the mediator is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void ConfirmSplit(UInvUI_ContainerMediatorComponent* Mediator, FInvUI_ContainerInstanceId ToContainer);

	// --- Observable getters ---
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") int32 GetSelected() const { return Selected; }
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") int32 GetMin() const { return Min; }
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") int32 GetMax() const { return Max; }
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") FGameplayTag GetSlotTag() const { return SlotTag; }
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") FInvUI_ContainerInstanceId GetSourceContainer() const { return SourceContainer; }
	/** True when there is a valid range to split over (Max >= Min >= 1). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter") bool IsValidSession() const { return Max >= Min && Min >= 1; }

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	void BroadcastField(EField Field);

	UPROPERTY(Transient) int32 Selected = 0;
	UPROPERTY(Transient) int32 Min = 1;
	UPROPERTY(Transient) int32 Max = 0;
	UPROPERTY(Transient) FGameplayTag SlotTag;
	UPROPERTY(Transient) FInvUI_ContainerInstanceId SourceContainer;
};
