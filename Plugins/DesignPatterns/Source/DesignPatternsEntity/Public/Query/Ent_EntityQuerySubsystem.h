// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "UObject/ScriptInterface.h"
#include "Ent_EntityQuerySubsystem.generated.h"

class UEnt_EntityComponent;
class UEnt_TagContainerComponent;
class UEnt_EntityRegistrySubsystem;

/**
 * Tag-query + spatial filtering world subsystem.
 *
 * A NEW sibling of UEnt_EntityRegistrySubsystem (the registry is sealed; a "partial extension" would be
 * illegal). It holds a side index of UEnt_TagContainerComponent by FSeam_EntityId (populated by that
 * component's register call), weakly references the registry for entity lookup, and weakly caches a
 * TScriptInterface<ISeam_TileProviderRead> for spatial narrowing.
 *
 * COMPOSITION
 *  - Tag queries match against the tag component's replicated FGameplayTagContainer view (server+clients).
 *  - Spatial queries narrow by world location; when a tile provider is set, region queries map cells to
 *    world AABBs via the seam, otherwise spatial narrowing simply falls back to a direct distance/AABB
 *    test on the actor location (an absent grid never breaks the query, it just skips cell mapping).
 *
 * Touches NO shipped header beyond the registry it weakly references.
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_EntityQuerySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on server / standalone, false on a network client (own helper, no base). */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- Tag-container index (called by UEnt_TagContainerComponent) ----------------------------

	/** Register a tag container so it participates in tag/spatial queries. Idempotent. */
	void RegisterTagContainer(UEnt_TagContainerComponent* Container);

	/** Unregister a tag container. */
	void UnregisterTagContainer(UEnt_TagContainerComponent* Container);

	// ---- Spatial provider ----------------------------------------------------------------------

	/**
	 * Set the grid/tile provider used to map region (cell) queries to world space. Held weakly; an
	 * absent or stale provider makes region queries fall back to direct world-space tests.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Query")
	void SetTileProvider(const TScriptInterface<ISeam_TileProviderRead>& InProvider);

	// ---- Queries -------------------------------------------------------------------------------

	/** Every registered entity component whose tags satisfy Query. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Query")
	void QueryEntitiesByTagQuery(const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const;

	/**
	 * Entities within Radius of Center whose tags satisfy Query (pass an empty query to match all
	 * tagged entities). Radius compared against the owning actor's world location.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Query")
	void QueryEntitiesInRadius(FVector Center, float Radius, const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const;

	/**
	 * Entities within the inclusive cell region [Min, Max] whose tags satisfy Query. Uses the tile
	 * provider to convert cells to a world AABB; if no provider is set, the region is treated as
	 * "match all tagged entities satisfying Query" (spatial narrowing skipped, never an error).
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Query")
	void QueryEntitiesInRegion(FSeam_CellCoord Min, FSeam_CellCoord Max, const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Registered tag containers keyed by entity id. Weak so the index never keeps an entity alive. */
	mutable TMap<FSeam_EntityId, TWeakObjectPtr<UEnt_TagContainerComponent>> TagContainersById;

	/** Weakly-cached registry for id->component lookup. */
	mutable TWeakObjectPtr<UEnt_EntityRegistrySubsystem> CachedRegistry;

	/** Weakly-cached grid provider for region->world mapping. */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_TileProviderRead> TileProvider;

	/** Resolve the registry (cached weakly). */
	UEnt_EntityRegistrySubsystem* GetRegistry() const;

	/** Drop stale (GC'd) tag-container slots. */
	void PruneStale() const;

	/** Resolve a tag container's owning entity component, or null. */
	static UEnt_EntityComponent* GetEntityFor(const UEnt_TagContainerComponent* Container);

	/** Convert an inclusive cell region to a world AABB via the tile provider; returns false if unavailable. */
	bool RegionToWorldBounds(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max, FBox& OutBounds) const;
};
