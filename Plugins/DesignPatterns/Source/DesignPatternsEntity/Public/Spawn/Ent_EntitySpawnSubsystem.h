// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Ent_EntitySpawnSubsystem.generated.h"

class AActor;
class UEnt_ArchetypeAsset;
class UEnt_EntityComponent;

/**
 * Archetype-driven spawn orchestration over the core spawn factory + object pool.
 *
 * AUTHORITY-ONLY (own GetNetMode check): entity spawning is a server decision; clients receive the
 * spawned actor via replication. Builds an FDP_SpawnParams from an archetype tag and routes it through
 * UDP_SpawnFactorySubsystem::Spawn (which may itself pool); then on the result's UEnt_EntityComponent
 * it sets the archetype tag and applies the archetype's trait set.
 *
 * WARM POOLS: warmup uses an AUTHORED TSoftClassPtr<AActor> (resolved to a TSubclassOf<AActor>) handed
 * to UDP_ObjectPoolSubsystem::WarmupAsync. It deliberately does NOT reverse-resolve the factory's
 * actor class — the designer states which class to warm per archetype.
 *
 * Both the factory and the pool are resolved by symbol but null-checked so the module still loads if a
 * project strips either subsystem (editor/preview worlds, or a trimmed build).
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_EntitySpawnSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	/** True on server / standalone, false on a network client (own helper, no base). */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Spawn an entity for ArchetypeTag at Transform. AUTHORITY ONLY. Uses DefaultFactoryTag (or
	 * ArchetypeTag itself if DefaultFactoryTag is unset) as the factory identity. Returns the spawned
	 * actor (with its entity component archetype-applied), or null on failure / non-authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Spawn")
	AActor* SpawnEntityFromArchetype(FGameplayTag ArchetypeTag, const FTransform& Transform, AActor* Owner);

	/**
	 * Spawn an entity from a resolved archetype ASSET (its DataTag drives identity). AUTHORITY ONLY.
	 * Applies the full asset (parent chain) to the spawned entity component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Spawn")
	AActor* SpawnEntityFromArchetypeAsset(UEnt_ArchetypeAsset* Asset, const FTransform& Transform, AActor* Owner);

	/**
	 * Pre-warm a pool of WarmClass for ArchetypeTag so the first spawns of that archetype do not hitch.
	 * AUTHORITY ONLY. WarmClass is authored by the caller (does not reverse-resolve the factory).
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Spawn")
	void WarmPoolForArchetype(FGameplayTag ArchetypeTag, TSubclassOf<AActor> WarmClass, int32 Count);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Factory identity tag used when SpawnEntityFromArchetype is called with only an archetype tag.
	 * When unset, the archetype tag itself is used as the factory identity. Tunable.
	 */
	UPROPERTY(EditAnywhere, Category = "Entity|Spawn")
	FGameplayTag DefaultFactoryTag;

	/** When true, spawns request pooling (FDP_SpawnParams::bAllowPooling). Tunable. */
	UPROPERTY(EditAnywhere, Category = "Entity|Spawn")
	bool bAllowPooling = true;

private:
	/** Common spawn path: build params, dispatch the factory, archetype-apply the result. */
	AActor* SpawnInternal(FGameplayTag FactoryTag, FGameplayTag ArchetypeTagToApply, UEnt_ArchetypeAsset* AssetToApply, const FTransform& Transform, AActor* Owner);

	/** Locate the entity component on a spawned actor (by class). */
	static UEnt_EntityComponent* FindEntityComponent(AActor* Actor);
};
