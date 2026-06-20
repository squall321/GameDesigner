// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Library/Ent_BlueprintLibrary.h"
#include "Entity/Ent_EntityComponent.h"
#include "Entity/Ent_Entity.h"
#include "Capability/Ent_CapabilityProvider.h"
#include "Trait/Ent_Trait.h"
#include "Registry/Ent_EntityRegistrySubsystem.h"

#include "Core/DPSubsystemLibrary.h"
#include "GameFramework/Actor.h"

UEnt_EntityComponent* UEnt_BlueprintLibrary::GetEntityComponent(AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	// Prefer the IEnt_Entity seam: an actor may expose its entity component without us having to
	// scan its components (and may proxy to a component on a different actor).
	if (Actor->Implements<UEnt_Entity>())
	{
		if (UEnt_EntityComponent* FromSeam = IEnt_Entity::Execute_GetEntityComponent(Actor))
		{
			return FromSeam;
		}
	}

	// Fallback: find the component directly.
	return Actor->FindComponentByClass<UEnt_EntityComponent>();
}

bool UEnt_BlueprintLibrary::IsEntity(AActor* Actor)
{
	return GetEntityComponent(Actor) != nullptr;
}

FSeam_EntityId UEnt_BlueprintLibrary::GetEntityId(AActor* Actor)
{
	if (const UEnt_EntityComponent* Component = GetEntityComponent(Actor))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Component);
	}
	return FSeam_EntityId::Invalid();
}

FGameplayTag UEnt_BlueprintLibrary::GetArchetypeTag(AActor* Actor)
{
	if (const UEnt_EntityComponent* Component = GetEntityComponent(Actor))
	{
		return ISeam_EntityIdentity::Execute_GetArchetypeTag(Component);
	}
	return FGameplayTag();
}

bool UEnt_BlueprintLibrary::HasCapability(AActor* Actor, FGameplayTag Capability)
{
	// Query through the capability seam so any provider (not just our component) answers.
	UEnt_EntityComponent* Component = GetEntityComponent(Actor);
	if (Component && Component->Implements<UEnt_CapabilityProvider>())
	{
		return IEnt_CapabilityProvider::Execute_HasCapability(Component, Capability);
	}
	return false;
}

UObject* UEnt_BlueprintLibrary::GetCapabilityObject(AActor* Actor, FGameplayTag Capability)
{
	UEnt_EntityComponent* Component = GetEntityComponent(Actor);
	if (Component && Component->Implements<UEnt_CapabilityProvider>())
	{
		return IEnt_CapabilityProvider::Execute_GetCapabilityObject(Component, Capability);
	}
	return nullptr;
}

void UEnt_BlueprintLibrary::GetProvidedCapabilities(AActor* Actor, FGameplayTagContainer& OutCapabilities)
{
	OutCapabilities.Reset();
	UEnt_EntityComponent* Component = GetEntityComponent(Actor);
	if (Component && Component->Implements<UEnt_CapabilityProvider>())
	{
		IEnt_CapabilityProvider::Execute_GetProvidedCapabilities(Component, OutCapabilities);
	}
}

UEnt_Trait* UEnt_BlueprintLibrary::GetTraitByClass(AActor* Actor, TSubclassOf<UEnt_Trait> TraitClass)
{
	if (UEnt_EntityComponent* Component = GetEntityComponent(Actor))
	{
		return Component->GetTraitByClass(TraitClass);
	}
	return nullptr;
}

bool UEnt_BlueprintLibrary::HasTraitOfClass(AActor* Actor, TSubclassOf<UEnt_Trait> TraitClass)
{
	return GetTraitByClass(Actor, TraitClass) != nullptr;
}

UEnt_Trait* UEnt_BlueprintLibrary::GetTraitByTag(AActor* Actor, FGameplayTag TraitClassTag)
{
	if (UEnt_EntityComponent* Component = GetEntityComponent(Actor))
	{
		return Component->FindTraitByTag(TraitClassTag);
	}
	return nullptr;
}

//~ Registry-backed lookups -------------------------------------------------------------------

UEnt_EntityRegistrySubsystem* UEnt_BlueprintLibrary::GetEntityRegistry(const UObject* WorldContextObject)
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(WorldContextObject);
}

UEnt_EntityComponent* UEnt_BlueprintLibrary::FindEntityById(const UObject* WorldContextObject, FSeam_EntityId EntityId)
{
	if (UEnt_EntityRegistrySubsystem* Registry = GetEntityRegistry(WorldContextObject))
	{
		return Registry->FindByEntityId(EntityId);
	}
	return nullptr;
}

AActor* UEnt_BlueprintLibrary::FindEntityActorById(const UObject* WorldContextObject, FSeam_EntityId EntityId)
{
	if (const UEnt_EntityComponent* Component = FindEntityById(WorldContextObject, EntityId))
	{
		return Component->GetOwner();
	}
	return nullptr;
}

TArray<UEnt_EntityComponent*> UEnt_BlueprintLibrary::GetAllEntitiesOfArchetype(const UObject* WorldContextObject, FGameplayTag ArchetypeTag)
{
	if (UEnt_EntityRegistrySubsystem* Registry = GetEntityRegistry(WorldContextObject))
	{
		return Registry->GetAllOfArchetype(ArchetypeTag);
	}
	return TArray<UEnt_EntityComponent*>();
}
