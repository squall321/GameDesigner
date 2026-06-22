// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_ContextMenuViewModel.generated.h"

/** Fired when the player commits a verb choice (the radial/list confirmed a selection). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteract_OnVerbChosen, FGameplayTag, Verb);

/**
 * MVVM ViewModel the radial / list context-menu UMG binds to.
 *
 * Built on the framework's lite UDP_ViewModelBase (FieldNotification, no MVVM-plugin dependency),
 * following the exact pattern of the shipped USaveX_SlotViewModel: observable values are exposed as
 * FieldNotify-tagged BlueprintPure getters, and setters broadcast the matching field id so any bound
 * view re-reads. The ViewModel holds NO gameplay pointers — only the value-typed FInteract_VerbMenu +
 * the chosen verb tag — so it is fully decoupled from the interactor/world and never replicated.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Interact Context Menu ViewModel"))
class DESIGNPATTERNSINTERACTION_API UInteract_ContextMenuViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Replace the whole menu (and reset the selection to the menu's default). Broadcasts both fields. */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void SetMenu(const FInteract_VerbMenu& InMenu);

	/** Change the highlighted selection index (clamped to the menu). Broadcasts the selection field. */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void SetSelectedIndex(int32 Index);

	/** Move the selection by Delta (wrapping), e.g. +1 / -1 for radial step. */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void StepSelection(int32 Delta);

	/** The verb record at Index, or a default-constructed one when out of range. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact|ContextMenu")
	FInteract_VerbAvailability GetVerbAt(int32 Index) const;

	/** The verb tag of the current selection, or an empty tag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact|ContextMenu")
	FGameplayTag GetSelectedVerb() const;

	/** Commit the current selection: broadcasts OnVerbChosen with the selected verb (if enabled). */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void CommitSelection();

	/** Fires when CommitSelection is called with a valid, enabled selection. */
	UPROPERTY(BlueprintAssignable, Category = "Interact|ContextMenu")
	FInteract_OnVerbChosen OnVerbChosen;

	// ---- Observable (FieldNotify) read accessors ----

	/** The current verb menu surface (observable). The widget binds its list to this. */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "Interact|ContextMenu")
	const FInteract_VerbMenu& GetMenu() const { return Menu; }

	/** The current selection index, or INDEX_NONE (observable). */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "Interact|ContextMenu")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** Number of verbs in the current menu (observable convenience for headers/counters). */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "Interact|ContextMenu")
	int32 GetVerbCount() const { return Menu.Verbs.Num(); }

	// FieldNotify ids generated for the FieldNotify-tagged getters above.
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(Menu);
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(SelectedIndex);
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(VerbCount);
	UE_FIELD_NOTIFICATION_DECLARE_ENUM_START(3)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(Menu)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(SelectedIndex)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(VerbCount)
	UE_FIELD_NOTIFICATION_DECLARE_ENUM_END()

private:
	/** The verb menu surface for the focused target. Backing storage for the observable getter. */
	UPROPERTY(Transient)
	FInteract_VerbMenu Menu;

	/** Index into Menu.Verbs of the highlighted selection, or INDEX_NONE. Backing storage. */
	UPROPERTY(Transient)
	int32 SelectedIndex = INDEX_NONE;
};
