// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Relationship/Ent_RelationshipComponent.h"
#include "Relationship/Ent_RelationshipSubsystem.h"
#include "Entity/Ent_EntityComponent.h"
#include "Entity/Ent_Entity.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Registry/Ent_EntityLifecycleMessage.h"
#include "DesignPatternsEntityTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UEnt_RelationshipComponent::UEnt_RelationshipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so per-entry callbacks can notify us (server and clients).
	Links.OwnerComponent = this;
}

void UEnt_RelationshipComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEnt_RelationshipComponent, Links);
}

void UEnt_RelationshipComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolve our identity from the sibling spine, then register whatever links exist (authored on
	// authority, or already replicated on a client).
	ResolveEntityComponent();
	RegisterAllLinksWithIndex();
}

void UEnt_RelationshipComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAllLinksFromIndex();
	Super::EndPlay(EndPlayReason);
}

//~ Identity / authority ----------------------------------------------------------------------

UEnt_EntityComponent* UEnt_RelationshipComponent::ResolveEntityComponent() const
{
	if (CachedEntityComponent.IsValid())
	{
		return CachedEntityComponent.Get();
	}

	UEnt_EntityComponent* Found = nullptr;
	if (AActor* Owner = GetOwner())
	{
		// Prefer the marker seam (lets non-trivial owners choose the component), fall back to a search.
		if (Owner->Implements<UEnt_Entity>())
		{
			Found = IEnt_Entity::Execute_GetEntityComponent(Owner);
		}
		if (!Found)
		{
			Found = Owner->FindComponentByClass<UEnt_EntityComponent>();
		}
	}
	// Mutable cache on a const path is fine: it is non-observable bookkeeping.
	const_cast<UEnt_RelationshipComponent*>(this)->CachedEntityComponent = Found;
	return Found;
}

FSeam_EntityId UEnt_RelationshipComponent::GetOwnEntityId() const
{
	if (UEnt_EntityComponent* Ent = ResolveEntityComponent())
	{
		// Read the id through the identity seam (the correct seam for a sibling's id).
		return ISeam_EntityIdentity::Execute_GetEntityId(Ent);
	}
	return FSeam_EntityId::Invalid();
}

bool UEnt_RelationshipComponent::HasEntityAuthority() const
{
	if (UEnt_EntityComponent* Ent = ResolveEntityComponent())
	{
		return Ent->HasEntityAuthority();
	}
	// Defensive fallback: defer to the owner's network authority.
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

//~ Mutators (authority only) -----------------------------------------------------------------

bool UEnt_RelationshipComponent::AddLink(FGameplayTag LinkKindTag, FSeam_EntityId TargetId, FSeam_NetValue Param)
{
	// AUTHORITY GUARD at the top.
	if (!HasEntityAuthority())
	{
		return false;
	}
	if (!LinkKindTag.IsValid() || !TargetId.IsValid())
	{
		return false;
	}

	// Owner links are one-to-one: replace any existing owner link first.
	const bool bIsOwnerKind = LinkKindTag.MatchesTagExact(EntNativeTags::Link_Owner);
	if (bIsOwnerKind)
	{
		RemoveAllLinksOfKind(EntNativeTags::Link_Owner);
	}

	if (FEnt_EntityLink* Existing = Links.FindLink(TargetId, LinkKindTag))
	{
		if (Existing->Param == Param)
		{
			return false; // No change.
		}
		Existing->Param = Param;
		Links.MarkItemDirty(*Existing);
	}
	else
	{
		FEnt_EntityLink& NewLink = Links.Links.AddDefaulted_GetRef();
		NewLink.TargetId = TargetId;
		NewLink.LinkKindTag = LinkKindTag;
		NewLink.Param = Param;
		Links.MarkItemDirty(NewLink);
	}

	// Authority updates the local index immediately (clients update via OnRep).
	if (UEnt_RelationshipSubsystem* Index = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_RelationshipSubsystem>(this))
	{
		FEnt_EntityLink Edge(TargetId, LinkKindTag, Param);
		Index->RegisterLink(GetOwnEntityId(), Edge);
	}

	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
	return true;
}

bool UEnt_RelationshipComponent::RemoveLink(FGameplayTag LinkKindTag, FSeam_EntityId TargetId)
{
	if (!HasEntityAuthority())
	{
		return false;
	}

	const FSeam_EntityId SelfId = GetOwnEntityId();
	const int32 Removed = Links.Links.RemoveAll([&](const FEnt_EntityLink& L)
	{
		return L.TargetId == TargetId && L.LinkKindTag == LinkKindTag;
	});
	if (Removed == 0)
	{
		return false;
	}

	Links.MarkArrayDirty();
	if (UEnt_RelationshipSubsystem* Index = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_RelationshipSubsystem>(this))
	{
		Index->UnregisterLink(SelfId, TargetId, LinkKindTag);
	}

	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
	return true;
}

int32 UEnt_RelationshipComponent::RemoveAllLinksOfKind(FGameplayTag LinkKindTag)
{
	if (!HasEntityAuthority())
	{
		return 0;
	}

	const FSeam_EntityId SelfId = GetOwnEntityId();
	TArray<FSeam_EntityId> RemovedTargets;
	for (const FEnt_EntityLink& L : Links.Links)
	{
		if (L.LinkKindTag == LinkKindTag)
		{
			RemovedTargets.Add(L.TargetId);
		}
	}
	if (RemovedTargets.Num() == 0)
	{
		return 0;
	}

	Links.Links.RemoveAll([&](const FEnt_EntityLink& L) { return L.LinkKindTag == LinkKindTag; });
	Links.MarkArrayDirty();

	if (UEnt_RelationshipSubsystem* Index = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_RelationshipSubsystem>(this))
	{
		for (const FSeam_EntityId& Target : RemovedTargets)
		{
			Index->UnregisterLink(SelfId, Target, LinkKindTag);
		}
	}

	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
	return RemovedTargets.Num();
}

int32 UEnt_RelationshipComponent::ClearAllLinks()
{
	if (!HasEntityAuthority())
	{
		return 0;
	}
	const int32 Count = Links.Links.Num();
	if (Count == 0)
	{
		return 0;
	}

	UnregisterAllLinksFromIndex();
	Links.Links.Reset();
	Links.MarkArrayDirty();

	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
	return Count;
}

void UEnt_RelationshipComponent::RestoreFromRecords(const TArray<FEnt_LinkRecord>& Records)
{
	// AUTHORITY GUARD: snapshot restore is server-driven; clients receive the result via replication.
	if (!HasEntityAuthority())
	{
		return;
	}

	UnregisterAllLinksFromIndex();
	Links.Links.Reset();

	for (const FEnt_LinkRecord& Record : Records)
	{
		if (!Record.IsValidRecord())
		{
			continue;
		}
		FEnt_EntityLink& NewLink = Links.Links.AddDefaulted_GetRef();
		NewLink.TargetId = Record.TargetId;
		NewLink.LinkKindTag = Record.LinkKindTag;
		NewLink.Param = Record.Param;
	}
	Links.MarkArrayDirty();

	RegisterAllLinksWithIndex();
	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
}

//~ Queries -----------------------------------------------------------------------------------

void UEnt_RelationshipComponent::GetLinks(FGameplayTag KindTag, TArray<FSeam_EntityId>& Out) const
{
	for (const FEnt_EntityLink& L : Links.Links)
	{
		if (!KindTag.IsValid() || L.LinkKindTag == KindTag)
		{
			Out.Add(L.TargetId);
		}
	}
}

FSeam_EntityId UEnt_RelationshipComponent::GetPrimaryOwner() const
{
	for (const FEnt_EntityLink& L : Links.Links)
	{
		if (L.LinkKindTag.MatchesTagExact(EntNativeTags::Link_Owner))
		{
			return L.TargetId;
		}
	}
	return FSeam_EntityId::Invalid();
}

bool UEnt_RelationshipComponent::HasLinkTo(FSeam_EntityId TargetId, FGameplayTag KindTag) const
{
	for (const FEnt_EntityLink& L : Links.Links)
	{
		if (L.TargetId == TargetId && (!KindTag.IsValid() || L.LinkKindTag == KindTag))
		{
			return true;
		}
	}
	return false;
}

void UEnt_RelationshipComponent::GetLinkRecords(TArray<FEnt_LinkRecord>& Out) const
{
	Out.Reserve(Out.Num() + Links.Links.Num());
	for (const FEnt_EntityLink& L : Links.Links)
	{
		Out.Add(L.ToRecord());
	}
}

//~ Replication / index sync ------------------------------------------------------------------

void UEnt_RelationshipComponent::OnRep_Links()
{
	// Client: link state changed. Rebuild the local index mirror and notify.
	HandleReplicatedLinkChange();
}

void UEnt_RelationshipComponent::HandleReplicatedLinkChange()
{
	// Re-register the full set (cheap: registration is idempotent) so the world index reflects the
	// current replicated state on this client without per-edge diffing.
	RegisterAllLinksWithIndex();
	OnLinksChanged.Broadcast(this);
	BroadcastLinksChanged();
}

void UEnt_RelationshipComponent::RegisterAllLinksWithIndex()
{
	const FSeam_EntityId SelfId = GetOwnEntityId();
	if (!SelfId.IsValid())
	{
		// Id not known yet (client awaiting OnRep_EntityId). We will be called again on link/identity rep.
		return;
	}
	UEnt_RelationshipSubsystem* Index = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_RelationshipSubsystem>(this);
	if (!Index)
	{
		return;
	}

	// Drop the previous snapshot for this source, then add the current links (handles removals too).
	Index->UnregisterAllFrom(SelfId);
	for (const FEnt_EntityLink& L : Links.Links)
	{
		if (L.IsValidEntry())
		{
			Index->RegisterLink(SelfId, L);
		}
	}
	bRegisteredWithIndex = true;
}

void UEnt_RelationshipComponent::UnregisterAllLinksFromIndex()
{
	if (!bRegisteredWithIndex)
	{
		return;
	}
	const FSeam_EntityId SelfId = GetOwnEntityId();
	if (SelfId.IsValid())
	{
		if (UEnt_RelationshipSubsystem* Index = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_RelationshipSubsystem>(this))
		{
			Index->UnregisterAllFrom(SelfId);
		}
	}
	bRegisteredWithIndex = false;
}

void UEnt_RelationshipComponent::BroadcastLinksChanged() const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	const FEnt_EntityLifecycleMessage Message(
		EEnt_EntityLifecyclePhase::Registered, // phase is unused by link listeners; carry identity only
		GetOwnEntityId(),
		FGameplayTag());

	FInstancedStruct Payload;
	Payload.InitializeAs<FEnt_EntityLifecycleMessage>(Message);
	Bus->BroadcastPayload(EntNativeTags::Bus_LinksChanged, Payload, GetOwner());
}
