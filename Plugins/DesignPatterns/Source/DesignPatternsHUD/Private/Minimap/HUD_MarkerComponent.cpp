// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Minimap/HUD_MarkerComponent.h"

#include "Minimap/HUD_MarkerRegistrySubsystem.h"
#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "GameFramework/Actor.h"

UHUD_MarkerComponent::UHUD_MarkerComponent()
{
	// Cosmetic read-model component: it answers projection queries on demand and never ticks.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;

	// Default to the generic point-of-interest icon so an unconfigured marker still renders.
	MarkerTag = HUDTags::Marker_PointOfInterest;
}

void UHUD_MarkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UHUD_MarkerRegistrySubsystem* Registry = ResolveRegistry())
	{
		RegistryWeak = Registry;
		Registry->RegisterTrackable(TScriptInterface<IHUD_Trackable>(this));
		bRegistered = true;
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("HUD_MarkerComponent on '%s': no marker registry (editor/preview?)."),
			*GetNameSafe(GetOwner()));
	}
}

void UHUD_MarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

UHUD_MarkerRegistrySubsystem* UHUD_MarkerComponent::ResolveRegistry() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UHUD_MarkerRegistrySubsystem>(this);
}

void UHUD_MarkerComponent::NotifyRegistryChanged()
{
	if (UHUD_MarkerRegistrySubsystem* Registry = RegistryWeak.Get())
	{
		Registry->OnMarkerSetChanged.Broadcast();
	}
}

FVector UHUD_MarkerComponent::GetWorldLocation_Implementation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	return Owner->GetActorLocation() + FVector(0.f, 0.f, WorldLocationZOffset);
}

FGameplayTag UHUD_MarkerComponent::GetMarkerTag_Implementation() const
{
	return MarkerTag;
}

bool UHUD_MarkerComponent::IsVisibleOnMap_Implementation() const
{
	return bVisibleOnMap;
}

void UHUD_MarkerComponent::SetMarkerTag(FGameplayTag InMarkerTag)
{
	if (MarkerTag == InMarkerTag)
	{
		return;
	}
	MarkerTag = InMarkerTag;
	NotifyRegistryChanged();
}

void UHUD_MarkerComponent::SetVisibleOnMap(bool bInVisible)
{
	if (bVisibleOnMap == bInVisible)
	{
		return;
	}
	bVisibleOnMap = bInVisible;
	NotifyRegistryChanged();
}
