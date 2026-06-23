// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_GroupComponent.h"
#include "Crowd/SimAg_FormationSubsystem.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Core/DPSubsystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

USimAg_GroupComponent::USimAg_GroupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USimAg_GroupComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GroupId.IsValid())
	{
		RefreshSlot();
	}
}

void USimAg_GroupComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GroupId.IsValid())
	{
		if (USimAg_FormationSubsystem* Formation = GetFormationSubsystem())
		{
			Formation->ReleaseSlot(GroupId, ResolveAgentId());
		}
	}
	Super::EndPlay(EndPlayReason);
}

void USimAg_GroupComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_GroupComponent, GroupId);
	DOREPLIFETIME(USimAg_GroupComponent, bIsLeader);
}

void USimAg_GroupComponent::SetGroup(FSeam_EntityId InGroupId, bool bAsLeader)
{
	// AUTHORITY GUARD at top.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (GroupId == InGroupId && bIsLeader == bAsLeader)
	{
		return;
	}

	// Release the old slot before switching groups.
	if (GroupId.IsValid())
	{
		if (USimAg_FormationSubsystem* Formation = GetFormationSubsystem())
		{
			Formation->ReleaseSlot(GroupId, ResolveAgentId());
		}
	}

	GroupId = InGroupId;
	bIsLeader = bAsLeader;
	RefreshSlot();
	OnGroupChanged.Broadcast(this);
}

FVector USimAg_GroupComponent::GetFormationSlotWorld(const FVector& LeaderGoal) const
{
	if (!GroupId.IsValid() || bIsLeader)
	{
		return LeaderGoal; // ungrouped or the leader: steer straight at the goal
	}
	const USimAg_FormationSubsystem* Formation = GetFormationSubsystem();
	if (!Formation)
	{
		return LeaderGoal;
	}

	// Orient the formation toward the leader's goal from the agent's current position so slots fan out
	// behind the direction of travel.
	const AActor* Owner = GetOwner();
	const FVector AgentLoc = Owner ? Owner->GetActorLocation() : LeaderGoal;
	FVector Facing = (LeaderGoal - AgentLoc);
	Facing.Z = 0.f;
	const FRotator AnchorRot = Facing.IsNearlyZero() ? FRotator::ZeroRotator : Facing.Rotation();

	return Formation->ResolveSlotWorld(FormationTag, CachedSlotIndex, LeaderGoal, AnchorRot);
}

void USimAg_GroupComponent::OnRep_Group()
{
	RefreshSlot();
	OnGroupChanged.Broadcast(this);
}

FSeam_EntityId USimAg_GroupComponent::ResolveAgentId() const
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

USimAg_FormationSubsystem* USimAg_GroupComponent::GetFormationSubsystem() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimAg_FormationSubsystem>(this);
}

void USimAg_GroupComponent::RefreshSlot()
{
	if (!GroupId.IsValid())
	{
		CachedSlotIndex = 0;
		return;
	}
	if (USimAg_FormationSubsystem* Formation = GetFormationSubsystem())
	{
		CachedSlotIndex = Formation->AssignSlot(GroupId, ResolveAgentId());
	}
}
