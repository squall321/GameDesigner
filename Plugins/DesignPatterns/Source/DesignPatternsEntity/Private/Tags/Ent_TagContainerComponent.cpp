// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tags/Ent_TagContainerComponent.h"
#include "Query/Ent_EntityQuerySubsystem.h"
#include "Entity/Ent_EntityComponent.h"
#include "Entity/Ent_Entity.h"
#include "Identity/Seam_EntityIdentity.h"
#include "DesignPatternsEntityTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Registry/Ent_EntityLifecycleMessage.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UEnt_TagContainerComponent::UEnt_TagContainerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Tags.OwnerComponent = this;
}

void UEnt_TagContainerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UEnt_TagContainerComponent, Tags);
}

void UEnt_TagContainerComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveEntityComponent();
	RebuildTagView();
	RegisterWithQuery();
}

void UEnt_TagContainerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromQuery();
	Super::EndPlay(EndPlayReason);
}

//~ Identity / authority ----------------------------------------------------------------------

UEnt_EntityComponent* UEnt_TagContainerComponent::ResolveEntityComponent() const
{
	if (CachedEntityComponent.IsValid())
	{
		return CachedEntityComponent.Get();
	}
	UEnt_EntityComponent* Found = nullptr;
	if (AActor* Owner = GetOwner())
	{
		if (Owner->Implements<UEnt_Entity>())
		{
			Found = IEnt_Entity::Execute_GetEntityComponent(Owner);
		}
		if (!Found)
		{
			Found = Owner->FindComponentByClass<UEnt_EntityComponent>();
		}
	}
	const_cast<UEnt_TagContainerComponent*>(this)->CachedEntityComponent = Found;
	return Found;
}

FSeam_EntityId UEnt_TagContainerComponent::GetOwnEntityId() const
{
	if (UEnt_EntityComponent* Ent = ResolveEntityComponent())
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Ent);
	}
	return FSeam_EntityId::Invalid();
}

bool UEnt_TagContainerComponent::HasEntityAuthority() const
{
	if (UEnt_EntityComponent* Ent = ResolveEntityComponent())
	{
		return Ent->HasEntityAuthority();
	}
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

//~ Mutators (authority only) -----------------------------------------------------------------

int32 UEnt_TagContainerComponent::AddTag(FGameplayTag Tag, int32 CountDelta)
{
	if (!HasEntityAuthority() || !Tag.IsValid() || CountDelta <= 0)
	{
		return GetTagCount(Tag);
	}

	int32 NewCount;
	if (FEnt_ReplicatedTag* Existing = Tags.FindByTag(Tag))
	{
		NewCount = Existing->GetCount() + CountDelta;
		Existing->StackCount = FSeam_NetValue::MakeInt(NewCount);
		Tags.MarkItemDirty(*Existing);
	}
	else
	{
		FEnt_ReplicatedTag& NewEntry = Tags.Entries.AddDefaulted_GetRef();
		NewEntry.Tag = Tag;
		NewEntry.StackCount = FSeam_NetValue::MakeInt(CountDelta);
		NewCount = CountDelta;
		Tags.MarkItemDirty(NewEntry);
	}

	RebuildTagView();
	OnTagChanged.Broadcast(this, Tag);
	BroadcastTagChanged(Tag);
	return NewCount;
}

int32 UEnt_TagContainerComponent::RemoveTag(FGameplayTag Tag, int32 CountDelta)
{
	if (!HasEntityAuthority() || !Tag.IsValid())
	{
		return GetTagCount(Tag);
	}

	FEnt_ReplicatedTag* Existing = Tags.FindByTag(Tag);
	if (!Existing)
	{
		return 0;
	}

	int32 Remaining = 0;
	if (CountDelta <= 0 || Existing->GetCount() - CountDelta <= 0)
	{
		Tags.Entries.RemoveAll([&](const FEnt_ReplicatedTag& E) { return E.Tag.MatchesTagExact(Tag); });
		Tags.MarkArrayDirty();
	}
	else
	{
		Remaining = Existing->GetCount() - CountDelta;
		Existing->StackCount = FSeam_NetValue::MakeInt(Remaining);
		Tags.MarkItemDirty(*Existing);
	}

	RebuildTagView();
	OnTagChanged.Broadcast(this, Tag);
	BroadcastTagChanged(Tag);
	return Remaining;
}

void UEnt_TagContainerComponent::ClearTags()
{
	if (!HasEntityAuthority() || Tags.Entries.Num() == 0)
	{
		return;
	}
	Tags.Entries.Reset();
	Tags.MarkArrayDirty();
	RebuildTagView();
	OnTagChanged.Broadcast(this, FGameplayTag());
	BroadcastTagChanged(FGameplayTag());
}

void UEnt_TagContainerComponent::SetTags(const FGameplayTagContainer& InTags)
{
	if (!HasEntityAuthority())
	{
		return;
	}
	Tags.Entries.Reset();
	for (const FGameplayTag& Tag : InTags)
	{
		if (Tag.IsValid())
		{
			FEnt_ReplicatedTag& NewEntry = Tags.Entries.AddDefaulted_GetRef();
			NewEntry.Tag = Tag;
			NewEntry.StackCount = FSeam_NetValue::MakeInt(1);
		}
	}
	Tags.MarkArrayDirty();
	RebuildTagView();
	OnTagChanged.Broadcast(this, FGameplayTag());
	BroadcastTagChanged(FGameplayTag());
}

//~ Queries -----------------------------------------------------------------------------------

bool UEnt_TagContainerComponent::HasTag(FGameplayTag Tag) const
{
	return CachedTagView.HasTagExact(Tag);
}

bool UEnt_TagContainerComponent::HasTagMatching(FGameplayTag Tag) const
{
	return CachedTagView.HasTag(Tag);
}

int32 UEnt_TagContainerComponent::GetTagCount(FGameplayTag Tag) const
{
	const FEnt_ReplicatedTag* Entry = Tags.FindByTag(Tag);
	return Entry ? Entry->GetCount() : 0;
}

void UEnt_TagContainerComponent::GetExplicitTags(FGameplayTagContainer& OutTags) const
{
	OutTags.AppendTags(CachedTagView);
}

bool UEnt_TagContainerComponent::MatchesQuery(const FGameplayTagQuery& Query) const
{
	// An empty query matches everything (the query subsystem relies on this for "match all tagged").
	return Query.IsEmpty() || Query.Matches(CachedTagView);
}

//~ Replication / index sync ------------------------------------------------------------------

void UEnt_TagContainerComponent::OnRep_Tags()
{
	RebuildTagView();
	// A coarse change notification (no single tag known) is fine on a bulk rep.
	HandleReplicatedTagChange(FGameplayTag());
}

void UEnt_TagContainerComponent::HandleReplicatedTagChange(FGameplayTag ChangedTag)
{
	RebuildTagView();
	OnTagChanged.Broadcast(this, ChangedTag);
	BroadcastTagChanged(ChangedTag);
}

void UEnt_TagContainerComponent::RebuildTagView()
{
	CachedTagView.Reset();
	for (const FEnt_ReplicatedTag& Entry : Tags.Entries)
	{
		if (Entry.Tag.IsValid())
		{
			CachedTagView.AddTag(Entry.Tag);
		}
	}
}

void UEnt_TagContainerComponent::RegisterWithQuery()
{
	if (bRegisteredWithQuery)
	{
		return;
	}
	if (UEnt_EntityQuerySubsystem* Query = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityQuerySubsystem>(this))
	{
		Query->RegisterTagContainer(this);
		bRegisteredWithQuery = true;
	}
}

void UEnt_TagContainerComponent::UnregisterFromQuery()
{
	if (!bRegisteredWithQuery)
	{
		return;
	}
	if (UEnt_EntityQuerySubsystem* Query = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_EntityQuerySubsystem>(this))
	{
		Query->UnregisterTagContainer(this);
	}
	bRegisteredWithQuery = false;
}

void UEnt_TagContainerComponent::BroadcastTagChanged(FGameplayTag ChangedTag) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	const FEnt_EntityLifecycleMessage Message(
		EEnt_EntityLifecyclePhase::Registered,
		GetOwnEntityId(),
		ChangedTag);

	FInstancedStruct Payload;
	Payload.InitializeAs<FEnt_EntityLifecycleMessage>(Message);
	Bus->BroadcastPayload(EntNativeTags::Bus_TagsChanged, Payload, GetOwner());
}
