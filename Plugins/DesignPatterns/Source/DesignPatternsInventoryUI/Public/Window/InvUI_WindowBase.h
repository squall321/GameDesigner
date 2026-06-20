// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "View/DPViewBase.h"
#include "GameplayTagContainer.h"

#include "Seam/InvUI_ItemContainer.h"

#include "InvUI_WindowBase.generated.h"

class UInvUI_GridWidget;
class UInvUI_GridViewModel;
class UInvUI_ContainerMediatorComponent;

/**
 * A pushable inventory WINDOW/panel — the unit the core UI mediator (UDP_UIManagerSubsystem)
 * pushes/pops on a layer.
 *
 * A window hosts one or more UInvUI_GridWidgets (a bag + a hotbar, a player doll + a chest, a
 * shop's stock + the player's purse, …). It is bound to a set of container instance ids; for
 * each id it locates the player-owned mediator once and binds the hosted grid that declares that
 * id's role, handing the grid the container id + mediator (the grid then resolves the live
 * container from the registry itself). On teardown (NativeDestruct, with the base BeginDestroy
 * backstop) it unbinds every grid so no field-changed / registry delegate outlives the window.
 *
 * Deriving from UDP_ViewBase, it also carries an optional window-level ViewModel (title, weight,
 * currency) and can publish tagged intents on the core message bus (e.g. "close requested");
 * container moves still flow exclusively through the mediator.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Window Base"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_WindowBase : public UDP_ViewBase
{
	GENERATED_BODY()

public:
	/**
	 * Bind this window to a set of container instance ids. Resolves the player-owned mediator and,
	 * for each id, the hosted grid that declares its role, then binds that grid to the id. Re-binding
	 * replaces any previous binding cleanly.
	 *
	 * @param InContainerIds  Stable ids of the containers this window should display.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Window")
	void BindContainers(const TArray<FInvUI_ContainerInstanceId>& InContainerIds);

	/**
	 * Bind a single container id to a specific hosted grid by the grid's role tag.
	 *
	 * @param RoleTag      Role of the target grid within this window.
	 * @param ContainerId  Stable id of the container to show in that grid.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Window")
	void BindContainerToRole(FGameplayTag RoleTag, FInvUI_ContainerInstanceId ContainerId);

	/** Unbind every hosted grid and clear the bound container set. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Window")
	void UnbindAllContainers();

	/** Register a hosted grid widget under a role tag so BindContainerToRole can target it. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Window")
	void RegisterHostedGrid(FGameplayTag RoleTag, UInvUI_GridWidget* GridWidget);

	/** The container ids this window is currently bound to, in bind order. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Window")
	const TArray<FInvUI_ContainerInstanceId>& GetBoundContainerIds() const { return BoundContainerIds; }

	/** The per-player mediator this window resolved (may be null until first bind). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Window")
	UInvUI_ContainerMediatorComponent* GetMediator() const { return Mediator.Get(); }

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/**
	 * Resolve the role tag for a container id when BindContainers is given a flat list. The default
	 * native implementation assigns ids to declared HostedGrids in stable tag order; override in BP
	 * for windows with explicit role rules.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "InvUI|Window", meta = (DisplayName = "Resolve Role For Container"))
	FGameplayTag ResolveRoleForContainer(FInvUI_ContainerInstanceId ContainerId, int32 OrderIndex);
	virtual FGameplayTag ResolveRoleForContainer_Implementation(FInvUI_ContainerInstanceId ContainerId, int32 OrderIndex);

	/** Designer hook fired after the window finishes (re)binding its containers. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Window", meta = (DisplayName = "On Containers Bound"))
	void OnContainersBound();

	/**
	 * Role-tagged grid widgets hosted by this window, declared by the designer (bound via BindWidget
	 * by name or registered through RegisterHostedGrid). Maps role tag -> grid.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InvUI|Window")
	TMap<FGameplayTag, TObjectPtr<UInvUI_GridWidget>> HostedGrids;

private:
	/** Resolve the player-owned mediator off this window's owning player controller/pawn. */
	UInvUI_ContainerMediatorComponent* ResolveMediator() const;

	/**
	 * Get (creating if needed) the grid ViewModel for a role, bind it to the live container
	 * resolved from the registry and the active item-display resolver, and hand it to the role's
	 * hosted grid widget. Logs and no-ops if the role/grid/container cannot be resolved.
	 */
	void BindRole(FGameplayTag RoleTag, FInvUI_ContainerInstanceId ContainerId);

	/** The mediator resolved at bind time; non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_ContainerMediatorComponent> Mediator;

	/** The container ids this window is bound to, in bind order. */
	UPROPERTY(Transient)
	TArray<FInvUI_ContainerInstanceId> BoundContainerIds;

	/** Per-role grid ViewModels this window owns (created on bind, kept alive while the window lives). */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UInvUI_GridViewModel>> GridViewModels;
};
