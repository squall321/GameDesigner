// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Snapshot/Ent_EntitySnapshotSubsystem.h"
#include "Entity/Ent_EntityComponent.h"
#include "Registry/Ent_EntityRegistrySubsystem.h"
#include "Relationship/Ent_RelationshipComponent.h"
#include "Tags/Ent_TagContainerComponent.h"
#include "Persist/Seam_Persistable.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UEnt_EntitySnapshotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RewindBuffers.Reset();
}

void UEnt_EntitySnapshotSubsystem::Deinitialize()
{
	RewindBuffers.Reset();
	Super::Deinitialize();
}

UEnt_EntityRegistrySubsystem* UEnt_EntitySnapshotSubsystem::GetRegistry() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(this);
}

double UEnt_EntitySnapshotSubsystem::GetCaptureTime() const
{
	// Use the world's time as a deterministic-enough capture stamp; no wall-clock, no magic numbers.
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds();
	}
	return 0.0;
}

//~ Capture -----------------------------------------------------------------------------------

FEnt_EntitySnapshot UEnt_EntitySnapshotSubsystem::CaptureFromComponent(UEnt_EntityComponent* Entity) const
{
	FEnt_EntitySnapshot Snapshot;
	if (!Entity)
	{
		return Snapshot;
	}

	Snapshot.EntityId = Entity->GetEntityId();
	Snapshot.SimTimeSeconds = GetCaptureTime();

	// Core record via the persistable seam (yields an FInstancedStruct wrapping FEnt_EntitySaveData).
	ISeam_Persistable::Execute_CaptureState(Entity, Snapshot.CoreSave);

	if (AActor* Owner = Entity->GetOwner())
	{
		Snapshot.Extra.Transform = Owner->GetActorTransform();

		// Relationship links, flattened to plain records.
		if (const UEnt_RelationshipComponent* Rel = Owner->FindComponentByClass<UEnt_RelationshipComponent>())
		{
			Rel->GetLinkRecords(Snapshot.Extra.Links);
		}
		// Replicated tag set (explicit tags).
		if (const UEnt_TagContainerComponent* TagComp = Owner->FindComponentByClass<UEnt_TagContainerComponent>())
		{
			TagComp->GetExplicitTags(Snapshot.Extra.ReplicatedTags);
		}
	}

	return Snapshot;
}

FEnt_EntitySnapshot UEnt_EntitySnapshotSubsystem::CaptureEntity(FSeam_EntityId EntityId) const
{
	if (UEnt_EntityRegistrySubsystem* Registry = GetRegistry())
	{
		if (UEnt_EntityComponent* Entity = Registry->FindByEntityId(EntityId))
		{
			return CaptureFromComponent(Entity);
		}
	}
	return FEnt_EntitySnapshot();
}

FEnt_WorldSnapshot UEnt_EntitySnapshotSubsystem::CaptureWorld() const
{
	FEnt_WorldSnapshot World;
	World.SimTimeSeconds = GetCaptureTime();
	if (UEnt_EntityRegistrySubsystem* Registry = GetRegistry())
	{
		for (UEnt_EntityComponent* Entity : Registry->GetAllEntities())
		{
			if (Entity)
			{
				World.Entities.Add(CaptureFromComponent(Entity));
			}
		}
	}
	return World;
}

//~ Restore (authority only) ------------------------------------------------------------------

bool UEnt_EntitySnapshotSubsystem::ApplyToComponent(UEnt_EntityComponent* Entity, const FEnt_EntitySnapshot& Snapshot)
{
	if (!Entity)
	{
		return false;
	}

	// Core state via the persistable seam (its RestoreState is itself authority-guarded).
	ISeam_Persistable::Execute_RestoreState(Entity, Snapshot.CoreSave);

	if (AActor* Owner = Entity->GetOwner())
	{
		// Restore transform (authority only path; clients receive movement via replication).
		Owner->SetActorTransform(Snapshot.Extra.Transform, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

		// Rebuild relationship links.
		if (UEnt_RelationshipComponent* Rel = Owner->FindComponentByClass<UEnt_RelationshipComponent>())
		{
			Rel->RestoreFromRecords(Snapshot.Extra.Links);
		}
		// Rebuild replicated tag set.
		if (UEnt_TagContainerComponent* TagComp = Owner->FindComponentByClass<UEnt_TagContainerComponent>())
		{
			TagComp->SetTags(Snapshot.Extra.ReplicatedTags);
		}
	}
	return true;
}

bool UEnt_EntitySnapshotSubsystem::RestoreEntity(FSeam_EntityId EntityId, const FEnt_EntitySnapshot& Snapshot)
{
	// AUTHORITY GUARD at the top.
	if (!HasWorldAuthority())
	{
		return false;
	}
	UEnt_EntityRegistrySubsystem* Registry = GetRegistry();
	if (!Registry)
	{
		return false;
	}
	UEnt_EntityComponent* Entity = Registry->FindByEntityId(EntityId);
	if (!Entity)
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Snapshot] RestoreEntity: id '%s' not present."), *EntityId.ToString());
		return false;
	}
	return ApplyToComponent(Entity, Snapshot);
}

int32 UEnt_EntitySnapshotSubsystem::RestoreWorld(const FEnt_WorldSnapshot& Snapshot)
{
	if (!HasWorldAuthority())
	{
		return 0;
	}
	int32 Restored = 0;
	for (const FEnt_EntitySnapshot& EntitySnap : Snapshot.Entities)
	{
		if (EntitySnap.IsValidSnapshot() && RestoreEntity(EntitySnap.EntityId, EntitySnap))
		{
			++Restored;
		}
	}
	return Restored;
}

//~ Rewind (authority only) -------------------------------------------------------------------

void UEnt_EntitySnapshotSubsystem::PushRewindSnapshot(FSeam_EntityId EntityId)
{
	if (!HasWorldAuthority() || !EntityId.IsValid())
	{
		return;
	}
	const FEnt_EntitySnapshot Snapshot = CaptureEntity(EntityId);
	if (!Snapshot.IsValidSnapshot())
	{
		return;
	}
	TArray<FEnt_EntitySnapshot>& Ring = RewindBuffers.FindOrAdd(EntityId);
	Ring.Add(Snapshot);

	const int32 Cap = FMath::Max(1, MaxRewindFrames);
	while (Ring.Num() > Cap)
	{
		Ring.RemoveAt(0); // Drop the oldest.
	}
}

bool UEnt_EntitySnapshotSubsystem::RewindTo(FSeam_EntityId EntityId, int32 FramesBack)
{
	if (!HasWorldAuthority() || FramesBack < 0)
	{
		return false;
	}
	const TArray<FEnt_EntitySnapshot>* Ring = RewindBuffers.Find(EntityId);
	if (!Ring || Ring->Num() == 0)
	{
		return false;
	}
	const int32 Index = Ring->Num() - 1 - FramesBack; // 0 frames back = most recent.
	if (!Ring->IsValidIndex(Index))
	{
		return false;
	}
	return RestoreEntity(EntityId, (*Ring)[Index]);
}

FString UEnt_EntitySnapshotSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalFrames = 0;
	for (const TPair<FSeam_EntityId, TArray<FEnt_EntitySnapshot>>& Pair : RewindBuffers)
	{
		TotalFrames += Pair.Value.Num();
	}
	return FString::Printf(TEXT("EntitySnapshot: %d rewind entities, %d frames (authority=%s)"),
		RewindBuffers.Num(), TotalFrames, HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
