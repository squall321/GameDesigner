// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Ent_BlueprintLibrary.generated.h"

class AActor;
class UEnt_EntityComponent;
class UEnt_Trait;
class UEnt_EntityRegistrySubsystem;

/**
 * Blueprint/C++ convenience helpers over the entity spine.
 *
 * These functions are deliberately tolerant: they accept any actor, find its UEnt_EntityComponent (or
 * resolve one via the IEnt_Entity seam / the registry by id), and answer capability/trait/identity
 * queries through the seams. Nothing here mutates replicated state — mutation goes through the
 * authority-guarded component API (and, for client intent, a player-owned RPC component).
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_BlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Find the entity component on an actor. Prefers the IEnt_Entity seam (if the actor implements it),
	 * otherwise falls back to FindComponentByClass. Returns null for a null actor or a non-entity actor.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity")
	static UEnt_EntityComponent* GetEntityComponent(AActor* Actor);

	/** True if the actor is an entity (has a UEnt_EntityComponent / implements IEnt_Entity). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity")
	static bool IsEntity(AActor* Actor);

	/** The stable entity id of an actor, or an invalid id if it is not an entity. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity")
	static FSeam_EntityId GetEntityId(AActor* Actor);

	/** The archetype tag of an actor, or an empty tag if it is not an entity. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity")
	static FGameplayTag GetArchetypeTag(AActor* Actor);

	/**
	 * True if the actor's entity advertises Capability (exact-or-child match), queried through the
	 * IEnt_CapabilityProvider seam so it works against any provider, not just this module's component.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Capability")
	static bool HasCapability(AActor* Actor, FGameplayTag Capability);

	/** The object backing a capability on the actor (e.g. a trait/component), or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Capability")
	static UObject* GetCapabilityObject(AActor* Actor, FGameplayTag Capability);

	/** Gather all capabilities advertised by the actor's entity into OutCapabilities. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Capability")
	static void GetProvidedCapabilities(AActor* Actor, FGameplayTagContainer& OutCapabilities);

	/**
	 * The first live trait of TraitClass on the actor's entity, or null. Read-only and client-safe.
	 * For mutation, call the authority-guarded component API directly.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait",
		meta = (DeterminesOutputType = "TraitClass"))
	static UEnt_Trait* GetTraitByClass(AActor* Actor, TSubclassOf<UEnt_Trait> TraitClass);

	/** True if the actor's entity has a live trait of TraitClass. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait")
	static bool HasTraitOfClass(AActor* Actor, TSubclassOf<UEnt_Trait> TraitClass);

	/** The first live trait whose TraitClassTag matches, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait")
	static UEnt_Trait* GetTraitByTag(AActor* Actor, FGameplayTag TraitClassTag);

	// ---- Registry-backed lookups ---------------------------------------------------------------

	/** Resolve an entity component by its stable id from the world entity registry, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry",
		meta = (WorldContext = "WorldContextObject"))
	static UEnt_EntityComponent* FindEntityById(const UObject* WorldContextObject, FSeam_EntityId EntityId);

	/** The actor owning the entity with the given id, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry",
		meta = (WorldContext = "WorldContextObject"))
	static AActor* FindEntityActorById(const UObject* WorldContextObject, FSeam_EntityId EntityId);

	/** Every live entity component of the given archetype (exact or child), from the registry. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Registry",
		meta = (WorldContext = "WorldContextObject"))
	static TArray<UEnt_EntityComponent*> GetAllEntitiesOfArchetype(const UObject* WorldContextObject, FGameplayTag ArchetypeTag);

	/** The world entity registry subsystem for a world-context object, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry",
		meta = (WorldContext = "WorldContextObject"))
	static UEnt_EntityRegistrySubsystem* GetEntityRegistry(const UObject* WorldContextObject);
};
