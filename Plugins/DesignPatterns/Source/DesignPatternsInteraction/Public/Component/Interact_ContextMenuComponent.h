// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_ContextMenuComponent.generated.h"

class UInteract_InteractorComponent;
class UInteract_ContextMenuViewModel;

/** Fired (locally) when a menu is opened, passing the ViewModel the widget should bind to. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteract_OnMenuOpened, UInteract_ContextMenuViewModel*, ViewModel);

/** Fired (locally) when the menu closes (confirm or cancel). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FInteract_OnMenuClosed);

/**
 * LOCAL UI orchestration component placed beside UInteract_InteractorComponent on the player pawn.
 *
 * It owns the radial/list context-menu lifecycle entirely on the owning client:
 *   - OpenMenuForFocus builds the focused interactable's FInteract_VerbMenu (via the interactor's
 *     GetFocusVerbMenu, which folds in the ISeam_InteractAvailability seam) and pushes it into a
 *     freshly-spawned UInteract_ContextMenuViewModel for the UMG to bind to.
 *   - ConfirmSelection routes the chosen verb to the interactor's targeted ServerInteractAt path so
 *     the SERVER re-validates the named candidate — the menu never authorises an interaction itself.
 *
 * Local and never replicated: it reads already-local focus state and only issues client->server
 * intent through the interactor's validated RPC. On a dedicated server / remote proxy it is inert.
 */
UCLASS(ClassGroup = (DesignPatternsInteraction), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Cooking, Activation))
class DESIGNPATTERNSINTERACTION_API UInteract_ContextMenuComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteract_ContextMenuComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Open the context menu for the interactor's current focus. Builds the verb menu, spawns/refreshes
	 * the ViewModel, and fires OnMenuOpened. No-op when there is no focus or no menu entries.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void OpenMenuForFocus();

	/** Confirm the ViewModel's current selection: routes the chosen verb to the interactor (targeted). */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void ConfirmSelection();

	/** Close the menu without acting. Fires OnMenuClosed. */
	UFUNCTION(BlueprintCallable, Category = "Interact|ContextMenu")
	void CancelMenu();

	/** True while a menu is open. */
	UFUNCTION(BlueprintPure, Category = "Interact|ContextMenu")
	bool IsMenuOpen() const { return bMenuOpen; }

	/** The live ViewModel the UMG widget should bind to (null while closed). */
	UFUNCTION(BlueprintPure, Category = "Interact|ContextMenu")
	UInteract_ContextMenuViewModel* GetActiveViewModel() const { return ActiveViewModel; }

	/** Fired (locally) when a menu is opened, passing the ViewModel the widget should bind to. */
	UPROPERTY(BlueprintAssignable, Category = "Interact|ContextMenu")
	FInteract_OnMenuOpened OnMenuOpened;

	/** Fired (locally) when the menu closes (confirm or cancel). */
	UPROPERTY(BlueprintAssignable, Category = "Interact|ContextMenu")
	FInteract_OnMenuClosed OnMenuClosed;

protected:
	/** ViewModel class to instantiate. Designers can subclass for project-specific observable fields. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|ContextMenu")
	TSubclassOf<UInteract_ContextMenuViewModel> ViewModelClass;

private:
	/** Resolve the sibling interactor component on the owner. */
	UInteract_InteractorComponent* ResolveInteractor() const;

	/** Bound to the ViewModel's OnVerbChosen so a UMG-driven commit also routes through the interactor. */
	UFUNCTION()
	void HandleVerbChosen(FGameplayTag Verb);

	/** The interactor this component drives. Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInteract_InteractorComponent> Interactor;

	/** The live ViewModel (owned by this component while a menu is open). */
	UPROPERTY(Transient)
	TObjectPtr<UInteract_ContextMenuViewModel> ActiveViewModel;

	/** True while a menu is open. Local. */
	bool bMenuOpen = false;
};
