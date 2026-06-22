// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/Ent_EntityQuerySubsystem.h"
#include "Tags/Ent_TagContainerComponent.h"
#include "Entity/Ent_EntityComponent.h"
#include "Registry/Ent_EntityRegistrySubsystem.h"
#include "Grid/Seam_TileProviderRead.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UEnt_EntityQuerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TagContainersById.Reset();
}

void UEnt_EntityQuerySubsystem::Deinitialize()
{
	TagContainersById.Reset();
	CachedRegistry.Reset();
	TileProvider = nullptr;
	Super::Deinitialize();
}

//~ Index management --------------------------------------------------------------------------

void UEnt_EntityQuerySubsystem::RegisterTagContainer(UEnt_TagContainerComponent* Container)
{
	if (!Container)
	{
		return;
	}
	const FSeam_EntityId Id = Container->GetOwnEntityId();
	if (!Id.IsValid())
	{
		// Id not yet known (client awaiting OnRep_EntityId); the component re-registers on tag rep.
		return;
	}
	TagContainersById.Add(Id, Container);
}

void UEnt_EntityQuerySubsystem::UnregisterTagContainer(UEnt_TagContainerComponent* Container)
{
	if (!Container)
	{
		return;
	}
	const FSeam_EntityId Id = Container->GetOwnEntityId();
	if (Id.IsValid())
	{
		if (const TWeakObjectPtr<UEnt_TagContainerComponent>* Existing = TagContainersById.Find(Id))
		{
			if (Existing->Get() == Container)
			{
				TagContainersById.Remove(Id);
			}
		}
	}
}

void UEnt_EntityQuerySubsystem::SetTileProvider(const TScriptInterface<ISeam_TileProviderRead>& InProvider)
{
	TileProvider = InProvider;
}

UEnt_EntityRegistrySubsystem* UEnt_EntityQuerySubsystem::GetRegistry() const
{
	if (CachedRegistry.IsValid())
	{
		return CachedRegistry.Get();
	}
	UEnt_EntityRegistrySubsystem* Registry = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(this);
	CachedRegistry = Registry;
	return Registry;
}

void UEnt_EntityQuerySubsystem::PruneStale() const
{
	TArray<FSeam_EntityId> Dead;
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_TagContainerComponent>>& Pair : TagContainersById)
	{
		if (!Pair.Value.IsValid())
		{
			Dead.Add(Pair.Key);
		}
	}
	for (const FSeam_EntityId& Key : Dead)
	{
		TagContainersById.Remove(Key);
	}
}

UEnt_EntityComponent* UEnt_EntityQuerySubsystem::GetEntityFor(const UEnt_TagContainerComponent* Container)
{
	if (!Container)
	{
		return nullptr;
	}
	if (AActor* Owner = Container->GetOwner())
	{
		return Owner->FindComponentByClass<UEnt_EntityComponent>();
	}
	return nullptr;
}

//~ Queries -----------------------------------------------------------------------------------

void UEnt_EntityQuerySubsystem::QueryEntitiesByTagQuery(const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const
{
	PruneStale();
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_TagContainerComponent>>& Pair : TagContainersById)
	{
		UEnt_TagContainerComponent* Container = Pair.Value.Get();
		if (!Container || !Container->MatchesQuery(Query))
		{
			continue;
		}
		if (UEnt_EntityComponent* Ent = GetEntityFor(Container))
		{
			OutEntities.Add(Ent);
		}
	}
}

void UEnt_EntityQuerySubsystem::QueryEntitiesInRadius(FVector Center, float Radius, const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const
{
	PruneStale();
	const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_TagContainerComponent>>& Pair : TagContainersById)
	{
		UEnt_TagContainerComponent* Container = Pair.Value.Get();
		if (!Container || !Container->MatchesQuery(Query))
		{
			continue;
		}
		const AActor* Owner = Container->GetOwner();
		if (!Owner)
		{
			continue;
		}
		if (FVector::DistSquared(Owner->GetActorLocation(), Center) <= RadiusSq)
		{
			if (UEnt_EntityComponent* Ent = GetEntityFor(Container))
			{
				OutEntities.Add(Ent);
			}
		}
	}
}

bool UEnt_EntityQuerySubsystem::RegionToWorldBounds(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max, FBox& OutBounds) const
{
	if (!TileProvider)
	{
		return false;
	}
	UObject* ProviderObj = TileProvider.GetObject();
	if (!ProviderObj)
	{
		return false;
	}

	// Map the two corner cells to world space (corner, not center, so the AABB is inclusive of the
	// whole region), then expand to include the far edges of the max cell.
	const FVector MinCorner = ISeam_TileProviderRead::Execute_CellToWorld(ProviderObj, Min, /*bCenter=*/false);
	const FVector MaxCorner = ISeam_TileProviderRead::Execute_CellToWorld(ProviderObj, Max, /*bCenter=*/false);
	const float CellSize = ISeam_TileProviderRead::Execute_GetCellSize(ProviderObj);

	FVector Lo(FMath::Min(MinCorner.X, MaxCorner.X), FMath::Min(MinCorner.Y, MaxCorner.Y), -HALF_WORLD_MAX);
	FVector Hi(FMath::Max(MinCorner.X, MaxCorner.X), FMath::Max(MinCorner.Y, MaxCorner.Y), HALF_WORLD_MAX);
	// Include the full extent of the max cell.
	Hi.X += CellSize;
	Hi.Y += CellSize;

	OutBounds = FBox(Lo, Hi);
	return true;
}

void UEnt_EntityQuerySubsystem::QueryEntitiesInRegion(FSeam_CellCoord Min, FSeam_CellCoord Max, const FGameplayTagQuery& Query, TArray<UEnt_EntityComponent*>& OutEntities) const
{
	PruneStale();

	FBox Bounds;
	const bool bHaveBounds = RegionToWorldBounds(Min, Max, Bounds);
	if (!bHaveBounds)
	{
		// No grid provider: fall back to a pure tag query (spatial narrowing skipped, never an error).
		QueryEntitiesByTagQuery(Query, OutEntities);
		return;
	}

	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_TagContainerComponent>>& Pair : TagContainersById)
	{
		UEnt_TagContainerComponent* Container = Pair.Value.Get();
		if (!Container || !Container->MatchesQuery(Query))
		{
			continue;
		}
		const AActor* Owner = Container->GetOwner();
		if (Owner && Bounds.IsInsideOrOn(Owner->GetActorLocation()))
		{
			if (UEnt_EntityComponent* Ent = GetEntityFor(Container))
			{
				OutEntities.Add(Ent);
			}
		}
	}
}

//~ Debug -------------------------------------------------------------------------------------

FString UEnt_EntityQuerySubsystem::GetDPDebugString_Implementation() const
{
	PruneStale();
	return FString::Printf(TEXT("EntityQuery: %d tagged entities, tile-provider=%s"),
		TagContainersById.Num(), TileProvider.GetObject() ? TEXT("yes") : TEXT("no"));
}
