// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Social/SimAg_SocialComponent.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Identity/Seam_EntityRelationship.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ FSimAg_Affinity fast-array callbacks (clients only) -------------------------------------------

void FSimAg_Affinity::PostReplicatedAdd(const FSimAg_AffinityArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_Affinity::PostReplicatedChange(const FSimAg_AffinityArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_Affinity::PreReplicatedRemove(const FSimAg_AffinityArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimAg_SocialComponent ------------------------------------------------------------------------

USimAg_SocialComponent::USimAg_SocialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Affinities.OwnerComponent = this;
}

void USimAg_SocialComponent::BeginPlay()
{
	Super::BeginPlay();
	Affinities.OwnerComponent = this;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SeedFromRelationships();
	}
}

void USimAg_SocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_SocialComponent, Affinities);
}

//~ Reads (client-safe) ---------------------------------------------------------------------------

float USimAg_SocialComponent::GetAffinity(const FSeam_EntityId& Other) const
{
	const FSimAg_Affinity* Edge = FindEdge(Other);
	return Edge ? Edge->Value : 0.f;
}

bool USimAg_SocialComponent::FindBestLiked(const TArray<FSeam_EntityId>& Candidates, float MinAffinity, FSeam_EntityId& OutOther, float& OutValue) const
{
	bool bFound = false;
	float Best = MinAffinity;
	for (const FSeam_EntityId& Candidate : Candidates)
	{
		const float Value = GetAffinity(Candidate);
		if (Value >= Best)
		{
			Best = Value;
			OutOther = Candidate;
			OutValue = Value;
			bFound = true;
		}
	}
	return bFound;
}

//~ Mutators (authority only) ---------------------------------------------------------------------

float USimAg_SocialComponent::AdjustAffinity(const FSeam_EntityId& Other, float Delta)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		const FSimAg_Affinity* Edge = FindEdge(Other);
		return Edge ? Edge->Value : 0.f;
	}
	if (!Other.IsValid())
	{
		return 0.f;
	}

	if (FSimAg_Affinity* Edge = FindEdge(Other))
	{
		Edge->Value = FMath::Clamp(Edge->Value + Delta, -1.f, 1.f);
		MarkEdgeDirty(*Edge);
	}
	else
	{
		FSimAg_Affinity& Added = Affinities.Edges.Add_GetRef(FSimAg_Affinity(Other, FMath::Clamp(Delta, -1.f, 1.f)));
		MarkEdgeDirty(Added);
	}
	EmitSocialEvent(Other, Delta);
	return GetAffinity(Other);
}

void USimAg_SocialComponent::SetAffinity(const FSeam_EntityId& Other, float NewValue)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!Other.IsValid())
	{
		return;
	}
	const float Clamped = FMath::Clamp(NewValue, -1.f, 1.f);
	if (FSimAg_Affinity* Edge = FindEdge(Other))
	{
		Edge->Value = Clamped;
		MarkEdgeDirty(*Edge);
	}
	else
	{
		FSimAg_Affinity& Added = Affinities.Edges.Add_GetRef(FSimAg_Affinity(Other, Clamped));
		MarkEdgeDirty(Added);
	}
}

void USimAg_SocialComponent::HandleReplicatedChange()
{
	OnAffinityChanged.Broadcast(this);
}

//~ Internals -------------------------------------------------------------------------------------

FSimAg_Affinity* USimAg_SocialComponent::FindEdge(const FSeam_EntityId& Other)
{
	return Affinities.Edges.FindByPredicate([&Other](const FSimAg_Affinity& E) { return E.Other == Other; });
}

const FSimAg_Affinity* USimAg_SocialComponent::FindEdge(const FSeam_EntityId& Other) const
{
	return Affinities.Edges.FindByPredicate([&Other](const FSimAg_Affinity& E) { return E.Other == Other; });
}

FSeam_EntityId USimAg_SocialComponent::ResolveAgentId() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (const USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
		{
			return Agent->GetAgentId();
		}
	}
	return FSeam_EntityId::Invalid();
}

const ISeam_EntityRelationshipRead* USimAg_SocialComponent::ResolveRelationshipRead() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// ISeam_EntityRelationshipRead is raw-virtual (no UClass), so we cannot resolve it by interface tag.
	// Walk the world subsystems and cast each to the raw interface; the relationship subsystem (in the
	// Entity module) is the one that implements it. We never include the Entity concrete header.
	for (UWorldSubsystem* Sub : World->GetSubsystemArray<UWorldSubsystem>())
	{
		if (ISeam_EntityRelationshipRead* AsRead = Cast<ISeam_EntityRelationshipRead>(Sub))
		{
			if (AsRead->HasRelationshipIndex())
			{
				return AsRead;
			}
		}
	}
	return nullptr;
}

void USimAg_SocialComponent::SeedFromRelationships()
{
	if (!SeedLinkKind.IsValid())
	{
		return;
	}
	const FSeam_EntityId SelfId = ResolveAgentId();
	if (!SelfId.IsValid())
	{
		return;
	}
	const ISeam_EntityRelationshipRead* Read = ResolveRelationshipRead();
	if (!Read)
	{
		return;
	}

	TArray<FSeam_EntityId> Linked;
	Read->GetLinkedEntities(SelfId, SeedLinkKind, Linked);
	for (const FSeam_EntityId& Other : Linked)
	{
		if (Other.IsValid() && Other != SelfId && !FindEdge(Other))
		{
			FSimAg_Affinity& Added = Affinities.Edges.Add_GetRef(FSimAg_Affinity(Other, FMath::Clamp(SeedAffinity, -1.f, 1.f)));
			Affinities.MarkItemDirty(Added);
		}
	}
	OnAffinityChanged.Broadcast(this);
}

void USimAg_SocialComponent::MarkEdgeDirty(FSimAg_Affinity& Edge)
{
	Affinities.MarkItemDirty(Edge);
	OnAffinityChanged.Broadcast(this);
}

void USimAg_SocialComponent::EmitSocialEvent(const FSeam_EntityId& Other, float Delta) const
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FSimAg_SocialEvent Event;
		Event.Initiator = ResolveAgentId();
		Event.Other = Other;
		Event.Delta = Delta;

		FInstancedStruct Payload;
		Payload.InitializeAs<FSimAg_SocialEvent>(Event);
		Bus->BroadcastPayload(SimAgNativeTags::Bus_SocialInteraction, Payload, GetOwner());
	}
}
