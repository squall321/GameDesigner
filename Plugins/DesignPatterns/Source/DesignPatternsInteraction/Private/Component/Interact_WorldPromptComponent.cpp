// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Interact_WorldPromptComponent.h"

#include "Minimap/HUD_MarkerRegistrySubsystem.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

UInteract_WorldPromptComponent::UInteract_WorldPromptComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Cosmetic; never replicated (the owning actor already exists on every machine).
	SetIsReplicatedByDefault(false);
}

void UInteract_WorldPromptComponent::BeginPlay()
{
	Super::BeginPlay();

	// Self-register as a trackable so the HUD layer can render this prompt purely via the seam.
	if (UHUD_MarkerRegistrySubsystem* Registry = ResolveRegistry())
	{
		RegistryWeak = Registry;
		Registry->RegisterTrackable(TScriptInterface<IHUD_Trackable>(this));
		bRegistered = true;
	}
}

void UInteract_WorldPromptComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UHUD_MarkerRegistrySubsystem* Registry = RegistryWeak.Get())
		{
			Registry->UnregisterTrackable(TScriptInterface<IHUD_Trackable>(this));
		}
		bRegistered = false;
	}
	RegistryWeak.Reset();
	Super::EndPlay(EndPlayReason);
}

UHUD_MarkerRegistrySubsystem* UInteract_WorldPromptComponent::ResolveRegistry() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UHUD_MarkerRegistrySubsystem>(this);
}

void UInteract_WorldPromptComponent::NotifyRegistryChanged()
{
	if (UHUD_MarkerRegistrySubsystem* Registry = RegistryWeak.Get())
	{
		Registry->OnMarkerSetChanged.Broadcast();
	}
}

FVector UInteract_WorldPromptComponent::GetWorldLocation_Implementation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	// Anchor to a named socket if one is configured and present, else the actor origin; add the offset.
	if (SocketName != NAME_None)
	{
		if (const USceneComponent* Root = Owner->GetRootComponent())
		{
			if (Root->DoesSocketExist(SocketName))
			{
				const FTransform SocketTM = Root->GetSocketTransform(SocketName);
				return SocketTM.TransformPosition(LocalOffset);
			}
		}
	}

	return Owner->GetActorTransform().TransformPosition(LocalOffset);
}

FGameplayTag UInteract_WorldPromptComponent::GetMarkerTag_Implementation() const
{
	// When the focused verb is unavailable, prefer the unavailable style (falling back to the default).
	if (bFocusedLocally && !bFocusedVerbAvailable && UnavailableStyleTag.IsValid())
	{
		return UnavailableStyleTag;
	}
	return PromptStyleTag;
}

bool UInteract_WorldPromptComponent::IsVisibleOnMap_Implementation() const
{
	// When configured to show only while focused, gate visibility on the locally-pushed focus state.
	if (bVisibleOnlyWhenFocused)
	{
		return bFocusedLocally;
	}
	return true;
}

void UInteract_WorldPromptComponent::SetFocusedLocally(bool bFocused, const FInteract_VerbAvailability& Avail)
{
	const bool bChanged = (bFocusedLocally != bFocused) || (bFocusedVerbAvailable != Avail.bEnabled);

	bFocusedLocally = bFocused;
	bFocusedVerbAvailable = Avail.bEnabled;

	if (bChanged)
	{
		// Visibility / style may have flipped: refresh any live HUD ViewModel.
		NotifyRegistryChanged();
	}
}
