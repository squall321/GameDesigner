// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/Ent_EntityRegistrySubsystem.h"
#include "Registry/Ent_EntityLifecycleMessage.h"
#include "Entity/Ent_EntityComponent.h"
#include "Identity/Seam_EntityIdentity.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "DesignPatternsNativeTags.h"

#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// Message-bus channel for entity lifecycle, anchored under the DP bus root so tag-hierarchy
// matching lets a listener on "DP.Bus" receive these. Defined here (the registry owns this channel).
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_DP_Bus_Entity_Lifecycle, "DP.Bus.Entity.Lifecycle",
	"Message-bus channel on which the entity registry publishes FEnt_EntityLifecycleMessage.");

void UEnt_EntityRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EntitiesById.Reset();
}

void UEnt_EntityRegistrySubsystem::Deinitialize()
{
	EntitiesById.Reset();
	Super::Deinitialize();
}

void UEnt_EntityRegistrySubsystem::RegisterEntity(UEnt_EntityComponent* Entity)
{
	if (!Entity)
	{
		return;
	}
	const FSeam_EntityId Id = Entity->GetEntityId();
	if (!Id.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[EntityRegistry] Skipped registering '%s' with an invalid id."),
			*GetNameSafe(Entity->GetOwner()));
		return;
	}

	if (TWeakObjectPtr<UEnt_EntityComponent>* Existing = EntitiesById.Find(Id))
	{
		if (Existing->IsValid() && Existing->Get() != Entity)
		{
			UE_LOG(LogDP, Warning,
				TEXT("[EntityRegistry] Id collision: '%s' is already bound to '%s'; replacing with '%s'."),
				*Id.ToString(),
				*GetNameSafe(Existing->Get()->GetOwner()),
				*GetNameSafe(Entity->GetOwner()));
		}
		else if (Existing->Get() == Entity)
		{
			// Already registered (idempotent re-register, e.g. BeginPlay + OnRep ordering).
			return;
		}
	}

	EntitiesById.Add(Id, Entity);
	BroadcastLifecycle(Entity, /*bRegistered=*/true);
}

void UEnt_EntityRegistrySubsystem::UnregisterEntity(UEnt_EntityComponent* Entity)
{
	if (!Entity)
	{
		return;
	}
	const FSeam_EntityId Id = Entity->GetEntityId();
	if (!Id.IsValid())
	{
		return;
	}

	if (const TWeakObjectPtr<UEnt_EntityComponent>* Existing = EntitiesById.Find(Id))
	{
		// Only clear the slot if it still points at this component (a replaced slot is left alone).
		if (Existing->Get() == Entity)
		{
			BroadcastLifecycle(Entity, /*bRegistered=*/false);
			EntitiesById.Remove(Id);
		}
	}
}

UEnt_EntityComponent* UEnt_EntityRegistrySubsystem::FindByEntityId(FSeam_EntityId EntityId) const
{
	if (!EntityId.IsValid())
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UEnt_EntityComponent>* Found = EntitiesById.Find(EntityId))
	{
		if (Found->IsValid())
		{
			return Found->Get();
		}
		// Stale slot: schedule a prune (cannot mutate the map directly from this const path safely
		// while a caller may be iterating, so mark and prune at the next non-const opportunity).
		bPrunePending = true;
	}
	return nullptr;
}

bool UEnt_EntityRegistrySubsystem::IsRegistered(FSeam_EntityId EntityId) const
{
	return FindByEntityId(EntityId) != nullptr;
}

TArray<UEnt_EntityComponent*> UEnt_EntityRegistrySubsystem::GetAllOfArchetype(FGameplayTag ArchetypeTag) const
{
	TArray<UEnt_EntityComponent*> Result;
	if (!ArchetypeTag.IsValid())
	{
		return Result;
	}
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_EntityComponent>>& Pair : EntitiesById)
	{
		UEnt_EntityComponent* Component = Pair.Value.Get();
		if (Component && Component->GetArchetypeTag().MatchesTag(ArchetypeTag))
		{
			Result.Add(Component);
		}
	}
	return Result;
}

TArray<UEnt_EntityComponent*> UEnt_EntityRegistrySubsystem::GetAllEntities() const
{
	TArray<UEnt_EntityComponent*> Result;
	Result.Reserve(EntitiesById.Num());
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_EntityComponent>>& Pair : EntitiesById)
	{
		if (UEnt_EntityComponent* Component = Pair.Value.Get())
		{
			Result.Add(Component);
		}
	}
	return Result;
}

int32 UEnt_EntityRegistrySubsystem::GetEntityCount() const
{
	int32 Count = 0;
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_EntityComponent>>& Pair : EntitiesById)
	{
		if (Pair.Value.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

void UEnt_EntityRegistrySubsystem::BroadcastLifecycle(UEnt_EntityComponent* Entity, bool bRegistered) const
{
	if (!Entity)
	{
		return;
	}
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	// Resolve identity through the seam (the BlueprintNativeEvent thunks are non-const).
	const FEnt_EntityLifecycleMessage Message(
		bRegistered ? EEnt_EntityLifecyclePhase::Registered : EEnt_EntityLifecyclePhase::Unregistered,
		ISeam_EntityIdentity::Execute_GetEntityId(Entity),
		ISeam_EntityIdentity::Execute_GetArchetypeTag(Entity));

	FInstancedStruct Payload;
	Payload.InitializeAs<FEnt_EntityLifecycleMessage>(Message);

	// Instigator is the owning actor so listeners can hop to the entity if they wish.
	Bus->BroadcastPayload(TAG_DP_Bus_Entity_Lifecycle, Payload, Entity->GetOwner());
}

void UEnt_EntityRegistrySubsystem::PruneStale() const
{
	// Const-correct prune of a mutable bookkeeping map: collect dead keys, then erase.
	TArray<FSeam_EntityId> Dead;
	for (const TPair<FSeam_EntityId, TWeakObjectPtr<UEnt_EntityComponent>>& Pair : EntitiesById)
	{
		if (!Pair.Value.IsValid())
		{
			Dead.Add(Pair.Key);
		}
	}
	for (const FSeam_EntityId& Key : Dead)
	{
		EntitiesById.Remove(Key);
	}
	bPrunePending = false;
}

FString UEnt_EntityRegistrySubsystem::GetDPDebugString_Implementation() const
{
	if (bPrunePending)
	{
		PruneStale();
	}
	return FString::Printf(TEXT("EntityRegistry: %d entities (authority=%s)"),
		GetEntityCount(), HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
