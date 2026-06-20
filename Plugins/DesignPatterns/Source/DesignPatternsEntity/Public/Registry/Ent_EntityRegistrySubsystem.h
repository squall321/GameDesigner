// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Ent_EntityRegistrySubsystem.generated.h"

class UEnt_EntityComponent;
class UWorld;

/**
 * World-scoped index of all live entities, keyed by their stable FSeam_EntityId.
 *
 * Entities register on BeginPlay and unregister on EndPlay, so the registry tracks exactly the
 * entities present in the current world. Lookups return the entity COMPONENT (the spine), from which
 * a caller reaches identity, traits and capabilities. References are WEAK — the registry never keeps
 * an entity alive; stale slots are pruned lazily on access.
 *
 * Lifecycle is also surfaced on the DesignPatterns message bus (FEnt_EntityLifecycleMessage payload)
 * so systems can react to entities appearing/disappearing without polling.
 *
 * NOT replicated (subsystems never are). It is built independently on server and clients from the
 * already-replicated entity components, so both sides have a consistent local view keyed by the
 * replicated entity id.
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_EntityRegistrySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone, false
	 * on a network client. Registry indexing itself runs on all machines, but authority-only callers
	 * (e.g. spawners) can gate off this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Register an entity. Called from UEnt_EntityComponent::BeginPlay (and from OnRep_EntityId on
	 * clients once the replicated id arrives). Idempotent for a given (id, component) pair; a re-register
	 * with the same id but a different component replaces the slot and logs a warning. No-op for an
	 * invalid id or null component. Broadcasts a Registered lifecycle message on first registration.
	 */
	void RegisterEntity(UEnt_EntityComponent* Entity);

	/**
	 * Unregister an entity. Called from UEnt_EntityComponent::EndPlay. Only clears the slot if it still
	 * points at this component (so a replaced slot is not wrongly cleared). Broadcasts an Unregistered
	 * lifecycle message when a slot is actually removed.
	 */
	void UnregisterEntity(UEnt_EntityComponent* Entity);

	/** Resolve the entity component for an id, or null if absent / GC'd. Prunes the slot if stale. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry")
	UEnt_EntityComponent* FindByEntityId(FSeam_EntityId EntityId) const;

	/** True if a live entity with this id is currently registered. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry")
	bool IsRegistered(FSeam_EntityId EntityId) const;

	/** Every live entity whose archetype tag matches ArchetypeTag (exact or child of it). */
	UFUNCTION(BlueprintCallable, Category = "Entity|Registry")
	TArray<UEnt_EntityComponent*> GetAllOfArchetype(FGameplayTag ArchetypeTag) const;

	/** Snapshot of every currently-registered live entity component. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Registry")
	TArray<UEnt_EntityComponent*> GetAllEntities() const;

	/** Number of currently-registered (live or not-yet-pruned) entities. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Registry")
	int32 GetEntityCount() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Id -> entity component. Weak so the registry never keeps an entity alive. Mutable so const
	 * query/debug paths can lazily prune GC'd weak slots.
	 */
	mutable TMap<FSeam_EntityId, TWeakObjectPtr<UEnt_EntityComponent>> EntitiesById;

	/**
	 * Broadcast a lifecycle message on the bus. Builds an FEnt_EntityLifecycleMessage payload and
	 * publishes it on the lifecycle channel via the GameInstance message-bus subsystem.
	 */
	void BroadcastLifecycle(class UEnt_EntityComponent* Entity, bool bRegistered) const;

	/** Drop any stale (GC'd) weak slots. Const because it mutates only the mutable bookkeeping map. */
	void PruneStale() const;

	/** Mutable so const query paths can lazily prune dead weak entries. */
	mutable bool bPrunePending = false;
};
