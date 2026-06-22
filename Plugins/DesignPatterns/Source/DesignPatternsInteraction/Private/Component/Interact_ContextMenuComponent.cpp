// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Interact_ContextMenuComponent.h"
#include "Component/Interact_InteractorComponent.h"
#include "UI/Interact_ContextMenuViewModel.h"

#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UInteract_ContextMenuComponent::UInteract_ContextMenuComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Purely local UI orchestration; never replicated.
	SetIsReplicatedByDefault(false);

	ViewModelClass = UInteract_ContextMenuViewModel::StaticClass();
}

void UInteract_ContextMenuComponent::BeginPlay()
{
	Super::BeginPlay();
	Interactor = ResolveInteractor();
}

void UInteract_ContextMenuComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Close any open menu + drop the ViewModel so it can be GC'd.
	if (bMenuOpen)
	{
		CancelMenu();
	}
	ActiveViewModel = nullptr;
	Super::EndPlay(EndPlayReason);
}

UInteract_InteractorComponent* UInteract_ContextMenuComponent::ResolveInteractor() const
{
	if (Interactor.IsValid())
	{
		return Interactor.Get();
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UInteract_InteractorComponent>();
	}
	return nullptr;
}

void UInteract_ContextMenuComponent::OpenMenuForFocus()
{
	UInteract_InteractorComponent* Inter = ResolveInteractor();
	if (!Inter)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Interact] ContextMenu: no interactor component on owner."));
		return;
	}

	FInteract_VerbMenu VerbMenu;
	Inter->GetFocusVerbMenu(VerbMenu);
	if (VerbMenu.IsEmpty())
	{
		// Nothing focused / no verbs: do not open an empty menu.
		return;
	}

	// Lazily create the ViewModel (reused across opens).
	if (!ActiveViewModel)
	{
		const TSubclassOf<UInteract_ContextMenuViewModel> Class =
			ViewModelClass ? ViewModelClass : UInteract_ContextMenuViewModel::StaticClass();
		ActiveViewModel = NewObject<UInteract_ContextMenuViewModel>(this, Class);
		ActiveViewModel->OnVerbChosen.AddDynamic(this, &UInteract_ContextMenuComponent::HandleVerbChosen);
	}

	ActiveViewModel->SetMenu(VerbMenu);
	bMenuOpen = true;

	OnMenuOpened.Broadcast(ActiveViewModel);
}

void UInteract_ContextMenuComponent::ConfirmSelection()
{
	if (!bMenuOpen || !ActiveViewModel)
	{
		return;
	}
	// CommitSelection broadcasts OnVerbChosen → HandleVerbChosen routes to the interactor + closes.
	ActiveViewModel->CommitSelection();
}

void UInteract_ContextMenuComponent::CancelMenu()
{
	if (!bMenuOpen)
	{
		return;
	}
	bMenuOpen = false;
	OnMenuClosed.Broadcast();
}

void UInteract_ContextMenuComponent::HandleVerbChosen(FGameplayTag Verb)
{
	UInteract_InteractorComponent* Inter = ResolveInteractor();
	if (Inter && ActiveViewModel)
	{
		// Route the chosen verb against the focused target through the SERVER-VALIDATED targeted path.
		AActor* Target = ActiveViewModel->GetMenu().Target.Get();
		Inter->RequestInteractAt(Verb, Target);
	}

	// A confirmed selection closes the menu.
	if (bMenuOpen)
	{
		bMenuOpen = false;
		OnMenuClosed.Broadcast();
	}
}
