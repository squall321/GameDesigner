// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Relationship/Ent_RelationshipSubsystem.h"
#include "Relationship/Ent_RelationshipComponent.h"
#include "Registry/Ent_EntityRegistrySubsystem.h"
#include "Registry/Ent_EntityLifecycleMessage.h"
#include "Entity/Ent_EntityComponent.h"
#include "DesignPatternsEntityTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UEnt_RelationshipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Outgoing.Reset();
	Incoming.Reset();

	// Listen for entity unregister so we can prune dead links and cascade lifetime (authority only).
	// The registry owns the channel tag (defined file-locally in its .cpp); resolve it by name here so
	// we do not need to extern the symbol across translation units.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		const FGameplayTag LifecycleChannel =
			FGameplayTag::RequestGameplayTag(TEXT("DP.Bus.Entity.Lifecycle"), /*ErrorIfNotFound=*/false);
		if (LifecycleChannel.IsValid())
		{
			LifecycleListenerHandle = Bus->ListenNative(
				LifecycleChannel,
				[this](const FDP_Message& Message) { HandleEntityLifecycle(Message); },
				this);
		}
	}
}

void UEnt_RelationshipSubsystem::Deinitialize()
{
	// Remove the bus listener we registered (own every listener we create).
	if (LifecycleListenerHandle.IsValid())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			Bus->StopListening(LifecycleListenerHandle);
		}
		LifecycleListenerHandle = FDP_ListenerHandle();
	}

	Outgoing.Reset();
	Incoming.Reset();
	Super::Deinitialize();
}

//~ Registration ------------------------------------------------------------------------------

void UEnt_RelationshipSubsystem::RegisterLink(FSeam_EntityId From, const FEnt_EntityLink& Link)
{
	if (!From.IsValid() || !Link.IsValidEntry())
	{
		return;
	}

	TArray<FOutgoing>& Edges = Outgoing.FindOrAdd(From);
	const bool bAlready = Edges.ContainsByPredicate([&](const FOutgoing& E)
	{
		return E.Target == Link.TargetId && E.Kind == Link.LinkKindTag;
	});
	if (!bAlready)
	{
		Edges.Add(FOutgoing{ Link.TargetId, Link.LinkKindTag });
	}

	TArray<FOutgoing>& Back = Incoming.FindOrAdd(Link.TargetId);
	const bool bBackAlready = Back.ContainsByPredicate([&](const FOutgoing& E)
	{
		return E.Target == From && E.Kind == Link.LinkKindTag;
	});
	if (!bBackAlready)
	{
		// In the reverse index, Target stores the SOURCE so a children/group query can recover it.
		Back.Add(FOutgoing{ From, Link.LinkKindTag });
	}
}

void UEnt_RelationshipSubsystem::UnregisterLink(FSeam_EntityId From, FSeam_EntityId TargetId, FGameplayTag KindTag)
{
	if (TArray<FOutgoing>* Edges = Outgoing.Find(From))
	{
		Edges->RemoveAll([&](const FOutgoing& E) { return E.Target == TargetId && E.Kind == KindTag; });
		if (Edges->Num() == 0)
		{
			Outgoing.Remove(From);
		}
	}
	if (TArray<FOutgoing>* Back = Incoming.Find(TargetId))
	{
		Back->RemoveAll([&](const FOutgoing& E) { return E.Target == From && E.Kind == KindTag; });
		if (Back->Num() == 0)
		{
			Incoming.Remove(TargetId);
		}
	}
}

void UEnt_RelationshipSubsystem::UnregisterAllFrom(FSeam_EntityId From)
{
	if (TArray<FOutgoing>* Edges = Outgoing.Find(From))
	{
		// Mirror each removal out of the reverse index.
		for (const FOutgoing& E : *Edges)
		{
			if (TArray<FOutgoing>* Back = Incoming.Find(E.Target))
			{
				Back->RemoveAll([&](const FOutgoing& B) { return B.Target == From && B.Kind == E.Kind; });
				if (Back->Num() == 0)
				{
					Incoming.Remove(E.Target);
				}
			}
		}
		Outgoing.Remove(From);
	}
}

//~ Graph queries -----------------------------------------------------------------------------

void UEnt_RelationshipSubsystem::GetChildren(FSeam_EntityId Source, FGameplayTag KindTag, TArray<FSeam_EntityId>& Out) const
{
	if (const TArray<FOutgoing>* Back = Incoming.Find(Source))
	{
		for (const FOutgoing& E : *Back)
		{
			if (!KindTag.IsValid() || E.Kind == KindTag)
			{
				Out.AddUnique(E.Target); // E.Target is the SOURCE that links to us (i.e. a child).
			}
		}
	}
}

void UEnt_RelationshipSubsystem::GetOwnerChain(FSeam_EntityId Source, TArray<FSeam_EntityId>& Out) const
{
	TSet<FSeam_EntityId> Visited;
	Visited.Add(Source);

	FSeam_EntityId Current = Source;
	for (int32 Depth = 0; Depth < MaxOwnerChainDepth; ++Depth)
	{
		const FSeam_EntityId Owner = GetPrimaryOwner(Current);
		if (!Owner.IsValid() || Visited.Contains(Owner))
		{
			break; // No owner, or a cycle: stop.
		}
		Out.Add(Owner);
		Visited.Add(Owner);
		Current = Owner;
	}
}

void UEnt_RelationshipSubsystem::GetGroupMembers(FSeam_EntityId Source, TArray<FSeam_EntityId>& Out) const
{
	// Targets of Source's outgoing group links.
	if (const TArray<FOutgoing>* Edges = Outgoing.Find(Source))
	{
		for (const FOutgoing& E : *Edges)
		{
			if (E.Kind.MatchesTagExact(EntNativeTags::Link_Grouped))
			{
				Out.AddUnique(E.Target);
			}
		}
	}
	// Plus sources that name Source as a group target (symmetry for undirected-style groups).
	if (const TArray<FOutgoing>* Back = Incoming.Find(Source))
	{
		for (const FOutgoing& E : *Back)
		{
			if (E.Kind.MatchesTagExact(EntNativeTags::Link_Grouped))
			{
				Out.AddUnique(E.Target);
			}
		}
	}
}

//~ ISeam_EntityRelationshipRead --------------------------------------------------------------

bool UEnt_RelationshipSubsystem::HasRelationshipIndex() const
{
	return true;
}

int32 UEnt_RelationshipSubsystem::GetLinkedEntities(FSeam_EntityId Source, FGameplayTag LinkKindTag, TArray<FSeam_EntityId>& Out) const
{
	int32 Added = 0;
	if (const TArray<FOutgoing>* Edges = Outgoing.Find(Source))
	{
		for (const FOutgoing& E : *Edges)
		{
			if (!LinkKindTag.IsValid() || E.Kind == LinkKindTag)
			{
				Out.Add(E.Target);
				++Added;
			}
		}
	}
	return Added;
}

FSeam_EntityId UEnt_RelationshipSubsystem::GetPrimaryOwner(FSeam_EntityId Source) const
{
	if (const TArray<FOutgoing>* Edges = Outgoing.Find(Source))
	{
		for (const FOutgoing& E : *Edges)
		{
			if (E.Kind.MatchesTagExact(EntNativeTags::Link_Owner))
			{
				return E.Target;
			}
		}
	}
	return FSeam_EntityId::Invalid();
}

bool UEnt_RelationshipSubsystem::AreLinked(FSeam_EntityId A, FSeam_EntityId B, FGameplayTag LinkKindTag) const
{
	if (const TArray<FOutgoing>* Edges = Outgoing.Find(A))
	{
		for (const FOutgoing& E : *Edges)
		{
			if (E.Target == B && (!LinkKindTag.IsValid() || E.Kind == LinkKindTag))
			{
				return true;
			}
		}
	}
	return false;
}

//~ Lifecycle pruning / cascade (authority only) ----------------------------------------------

void UEnt_RelationshipSubsystem::HandleEntityLifecycle(const FDP_Message& Message)
{
	const FEnt_EntityLifecycleMessage* Payload = Message.Payload.GetPtr<FEnt_EntityLifecycleMessage>();
	if (!Payload || Payload->Phase != EEnt_EntityLifecyclePhase::Unregistered)
	{
		return;
	}
	const FSeam_EntityId Gone = Payload->EntityId;
	if (!Gone.IsValid())
	{
		return;
	}

	// Authority-only: cascade destruction to opted-in children before pruning the gone entity's edges.
	if (HasWorldAuthority())
	{
		DestroyChildrenOf(Gone);
	}

	// Prune the gone entity from BOTH indices on every machine (its own component already unregisters
	// its outgoing edges on EndPlay, but incoming edges from other entities must be cleaned too).
	UnregisterAllFrom(Gone);
	if (TArray<FOutgoing>* Back = Incoming.Find(Gone))
	{
		for (const FOutgoing& E : *Back)
		{
			if (TArray<FOutgoing>* SourceEdges = Outgoing.Find(E.Target))
			{
				SourceEdges->RemoveAll([&](const FOutgoing& S) { return S.Target == Gone; });
				if (SourceEdges->Num() == 0)
				{
					Outgoing.Remove(E.Target);
				}
			}
		}
		Incoming.Remove(Gone);
	}
}

void UEnt_RelationshipSubsystem::DestroyChildrenOf(FSeam_EntityId ParentId)
{
	UEnt_EntityRegistrySubsystem* Registry = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityRegistrySubsystem>(this);
	if (!Registry)
	{
		return;
	}

	// Children = entities that name ParentId as their primary owner.
	TArray<FSeam_EntityId> Children;
	GetChildren(ParentId, EntNativeTags::Link_Owner, Children);

	for (const FSeam_EntityId& ChildId : Children)
	{
		UEnt_EntityComponent* ChildEnt = Registry->FindByEntityId(ChildId);
		if (!ChildEnt)
		{
			continue;
		}
		AActor* ChildActor = ChildEnt->GetOwner();
		if (!ChildActor)
		{
			continue;
		}
		// Honour the child's opt-in policy on its relationship component.
		const UEnt_RelationshipComponent* ChildRel = ChildActor->FindComponentByClass<UEnt_RelationshipComponent>();
		if (ChildRel && ChildRel->bDestroyChildrenWithParent && !ChildActor->IsActorBeingDestroyed())
		{
			UE_LOG(LogDP, Verbose, TEXT("[Relationship] Cascading destroy of child '%s' (owner %s gone)."),
				*GetNameSafe(ChildActor), *ParentId.ToString());
			ChildActor->Destroy();
		}
	}
}

//~ Debug -------------------------------------------------------------------------------------

FString UEnt_RelationshipSubsystem::GetDPDebugString_Implementation() const
{
	int32 EdgeCount = 0;
	for (const TPair<FSeam_EntityId, TArray<FOutgoing>>& Pair : Outgoing)
	{
		EdgeCount += Pair.Value.Num();
	}
	return FString::Printf(TEXT("RelationshipIndex: %d sources, %d edges (authority=%s)"),
		Outgoing.Num(), EdgeCount, HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
