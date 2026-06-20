// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Window/InvUI_WindowBase.h"
#include "Widget/InvUI_GridWidget.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"

#include "ViewModel/InvUI_GridViewModel.h"
#include "Registry/InvUI_ContainerRegistry.h"
#include "Seam/InvUI_ItemDisplay.h"
#include "InvUI_NativeTags.h"

#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UInvUI_WindowBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Resolve the mediator up front; ResolveMediator re-resolves lazily if this misses.
	Mediator = ResolveMediator();
}

void UInvUI_WindowBase::NativeDestruct()
{
	// Clean unbind: detach every hosted grid so no ViewModel/registry delegate keeps a dead window.
	UnbindAllContainers();
	Super::NativeDestruct();
}

void UInvUI_WindowBase::RegisterHostedGrid(FGameplayTag RoleTag, UInvUI_GridWidget* GridWidget)
{
	if (!RoleTag.IsValid() || !GridWidget)
	{
		return;
	}
	HostedGrids.Add(RoleTag, GridWidget);
}

UInvUI_ContainerMediatorComponent* UInvUI_WindowBase::ResolveMediator() const
{
	if (UInvUI_ContainerMediatorComponent* Cached = Mediator.Get())
	{
		return Cached;
	}

	// The mediator lives on a PLAYER-OWNED actor: prefer the player controller, fall back to its pawn.
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return nullptr;
	}

	if (UInvUI_ContainerMediatorComponent* OnPC = PC->FindComponentByClass<UInvUI_ContainerMediatorComponent>())
	{
		return OnPC;
	}

	if (APawn* Pawn = PC->GetPawn())
	{
		if (UInvUI_ContainerMediatorComponent* OnPawn = Pawn->FindComponentByClass<UInvUI_ContainerMediatorComponent>())
		{
			return OnPawn;
		}
	}

	return nullptr;
}

FGameplayTag UInvUI_WindowBase::ResolveRoleForContainer_Implementation(
	FInvUI_ContainerInstanceId /*ContainerId*/, int32 OrderIndex)
{
	// Default policy: assign incoming containers to declared hosted grids in stable tag order.
	if (HostedGrids.Num() == 0)
	{
		return FGameplayTag();
	}

	TArray<FGameplayTag> Roles;
	HostedGrids.GenerateKeyArray(Roles);
	Roles.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.ToString() < B.ToString(); });

	return Roles.IsValidIndex(OrderIndex) ? Roles[OrderIndex] : FGameplayTag();
}

void UInvUI_WindowBase::BindContainers(const TArray<FInvUI_ContainerInstanceId>& InContainerIds)
{
	UnbindAllContainers();

	Mediator = ResolveMediator();
	BoundContainerIds = InContainerIds;

	for (int32 Index = 0; Index < InContainerIds.Num(); ++Index)
	{
		const FInvUI_ContainerInstanceId& Id = InContainerIds[Index];
		const FGameplayTag Role = ResolveRoleForContainer(Id, Index);
		BindRole(Role, Id);
	}

	OnContainersBound();
}

void UInvUI_WindowBase::BindContainerToRole(FGameplayTag RoleTag, FInvUI_ContainerInstanceId ContainerId)
{
	if (!Mediator.IsValid())
	{
		Mediator = ResolveMediator();
	}

	BoundContainerIds.AddUnique(ContainerId);
	BindRole(RoleTag, ContainerId);
	OnContainersBound();
}

void UInvUI_WindowBase::BindRole(FGameplayTag RoleTag, FInvUI_ContainerInstanceId ContainerId)
{
	if (!RoleTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Window '%s' has no hosted grid role for container %s."),
			*GetName(), *ContainerId.ToString());
		return;
	}

	TObjectPtr<UInvUI_GridWidget>* GridPtr = HostedGrids.Find(RoleTag);
	if (!GridPtr || !*GridPtr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Window '%s' has no hosted grid registered for role %s."),
			*GetName(), *RoleTag.ToString());
		return;
	}

	// Resolve the live container from the registry. A container that has not registered yet leaves
	// the grid bound to an empty ViewModel; the grid VM refreshes when the registry signals it.
	UInvUI_ContainerRegistry* Registry = UInvUI_ContainerRegistry::Get(this);
	TScriptInterface<IInvUI_ItemContainer> Container;
	if (Registry)
	{
		Container = Registry->ResolveContainer(ContainerId);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Window '%s' could not resolve the container registry."), *GetName());
	}

	// Resolve the active item-display resolver from the service locator (optional — null = no icons).
	TScriptInterface<IInvUI_ItemDisplay> DisplayResolver;
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* ResolverObj = Locator->ResolveService(InvUITags::Service_ItemDisplay))
		{
			if (ResolverObj->GetClass()->ImplementsInterface(UInvUI_ItemDisplay::StaticClass()))
			{
				DisplayResolver.SetObject(ResolverObj);
				DisplayResolver.SetInterface(Cast<IInvUI_ItemDisplay>(ResolverObj));
			}
		}
	}

	// Get-or-create the per-role grid ViewModel owned by this window, bind it to the container, and
	// hand it to the role's grid widget.
	TObjectPtr<UInvUI_GridViewModel>& GridVM = GridViewModels.FindOrAdd(RoleTag);
	if (!GridVM)
	{
		GridVM = NewObject<UInvUI_GridViewModel>(this);
	}

	// No adapter object is supplied here: the grid VM falls back to the container seam's own change
	// delegate. A genre integration that needs an adapter binds the VM itself before pushing the
	// window, or registers an adapter as the container.
	GridVM->BindContainer(Container, /*Adapter*/ nullptr, DisplayResolver);

	(*GridPtr)->BindGrid(GridVM, Mediator.Get());
}

void UInvUI_WindowBase::UnbindAllContainers()
{
	for (TPair<FGameplayTag, TObjectPtr<UInvUI_GridWidget>>& Pair : HostedGrids)
	{
		if (Pair.Value)
		{
			Pair.Value->UnbindGrid();
		}
	}
	for (TPair<FGameplayTag, TObjectPtr<UInvUI_GridViewModel>>& Pair : GridViewModels)
	{
		if (Pair.Value)
		{
			Pair.Value->UnbindContainer();
		}
	}
	BoundContainerIds.Reset();
}
