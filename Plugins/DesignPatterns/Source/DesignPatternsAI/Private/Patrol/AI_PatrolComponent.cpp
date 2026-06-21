// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Patrol/AI_PatrolComponent.h"
#include "Perception/AI_PerceptionComponent.h"
#include "Seams/AI_Brain.h"
#include "Settings/AI_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "FSM/DPBlackboard.h"

#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UAI_PatrolComponent::UAI_PatrolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UAI_PatrolComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthoritySafe())
	{
		// Bind perception so a fresh percept can divert us to investigate.
		if (UAI_PerceptionComponent* Perception = ResolvePerception())
		{
			Perception->OnPerceptUpdated.AddDynamic(this, &UAI_PatrolComponent::HandlePerceptUpdated);
		}

		if (bAutoStart && DefaultRoute)
		{
			StartPatrol(DefaultRoute);
		}
	}
}

void UAI_PatrolComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthoritySafe())
	{
		if (UAI_PerceptionComponent* Perception = ResolvePerception())
		{
			Perception->OnPerceptUpdated.RemoveDynamic(this, &UAI_PatrolComponent::HandlePerceptUpdated);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UAI_PatrolComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAI_PatrolComponent, CurrentWaypointIndex);
}

//~ Lifecycle -----------------------------------------------------------------------------------

void UAI_PatrolComponent::StartPatrol(UAI_PatrolRouteDataAsset* Route)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	ActiveRoute = Route ? Route : DefaultRoute.Get();
	if (!ActiveRoute || ActiveRoute->Waypoints.Num() == 0)
	{
		bPatrolling = false;
		return;
	}

	if (const AActor* Owner = GetOwner())
	{
		StartTransform = Owner->GetActorTransform();
	}

	CurrentWaypointIndex = 0;
	PingPongDir = 1;
	bWaiting = false;
	bInvestigating = false;
	bPatrolling = true;
	WaitTimer = 0.f;

	PushGoalToBlackboard(GetWaypointWorldLocation(CurrentWaypointIndex));
}

void UAI_PatrolComponent::StopPatrol()
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	bPatrolling = false;
	bInvestigating = false;
	bWaiting = false;
	if (IDP_BlackboardProvider* Board = ResolveBlackboardProvider())
	{
		Board->ClearKey(BlackboardKey_PatrolGoal);
	}
}

void UAI_PatrolComponent::InvestigateLocation(FVector WorldLocation)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	bInvestigating = true;
	bWaiting = false;
	InvestigateTarget = WorldLocation;
	PushGoalToBlackboard(WorldLocation);
}

//~ Tick walking --------------------------------------------------------------------------------

void UAI_PatrolComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasAuthoritySafe() || !bPatrolling || !ActiveRoute)
	{
		return;
	}
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Effective acceptance radius (project default when unset).
	float Acceptance = AcceptanceRadius;
	if (Acceptance <= 0.f)
	{
		if (const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get())
		{
			Acceptance = Settings->DefaultPatrolAcceptanceRadius;
		}
	}
	Acceptance = FMath::Max(1.f, Acceptance);

	// Wait countdown at a waypoint / investigation point.
	if (bWaiting)
	{
		WaitTimer -= DeltaTime;
		if (WaitTimer <= 0.f)
		{
			bWaiting = false;
			if (bInvestigating)
			{
				// Finished investigating; resume the route at the current waypoint.
				bInvestigating = false;
				PushGoalToBlackboard(GetWaypointWorldLocation(CurrentWaypointIndex));
			}
			else
			{
				AdvanceWaypoint();
				PushGoalToBlackboard(GetWaypointWorldLocation(CurrentWaypointIndex));
			}
		}
		return;
	}

	const FVector OwnerLoc = Owner->GetActorLocation();
	const FVector Goal = bInvestigating ? InvestigateTarget : GetWaypointWorldLocation(CurrentWaypointIndex);

	if (FVector::DistSquared(OwnerLoc, Goal) <= FMath::Square(Acceptance))
	{
		if (bInvestigating)
		{
			// Linger at the investigation point, then resume the route.
			bWaiting = true;
			WaitTimer = FMath::Max(0.f, InvestigateLingerSeconds);
		}
		else
		{
			// Reached a waypoint: honour its wait, else advance immediately.
			const FAI_PatrolWaypoint& WP = ActiveRoute->Waypoints[FMath::Clamp(CurrentWaypointIndex, 0, ActiveRoute->Waypoints.Num() - 1)];
			if (WP.WaitSeconds > 0.f)
			{
				bWaiting = true;
				WaitTimer = WP.WaitSeconds;
			}
			else
			{
				AdvanceWaypoint();
				PushGoalToBlackboard(GetWaypointWorldLocation(CurrentWaypointIndex));
			}
		}
	}
}

//~ Helpers -------------------------------------------------------------------------------------

FVector UAI_PatrolComponent::GetWaypointWorldLocation(int32 Index) const
{
	if (!ActiveRoute || !ActiveRoute->Waypoints.IsValidIndex(Index))
	{
		return StartTransform.GetLocation();
	}
	return StartTransform.TransformPosition(ActiveRoute->Waypoints[Index].RelativeLocation);
}

void UAI_PatrolComponent::AdvanceWaypoint()
{
	if (!ActiveRoute || ActiveRoute->Waypoints.Num() == 0)
	{
		return;
	}
	const int32 Count = ActiveRoute->Waypoints.Num();

	switch (ActiveRoute->Mode)
	{
	case EAI_PatrolMode::Once:
		CurrentWaypointIndex = FMath::Min(CurrentWaypointIndex + 1, Count - 1);
		if (CurrentWaypointIndex == Count - 1)
		{
			// Reached the end; stop walking but stay "patrolling" at the last point.
			bPatrolling = (Count > 1) ? bPatrolling : bPatrolling;
		}
		break;

	case EAI_PatrolMode::Loop:
		CurrentWaypointIndex = (CurrentWaypointIndex + 1) % Count;
		break;

	case EAI_PatrolMode::PingPong:
		if (Count > 1)
		{
			if (CurrentWaypointIndex + PingPongDir < 0 || CurrentWaypointIndex + PingPongDir >= Count)
			{
				PingPongDir = -PingPongDir;
			}
			CurrentWaypointIndex = FMath::Clamp(CurrentWaypointIndex + PingPongDir, 0, Count - 1);
		}
		break;

	case EAI_PatrolMode::Random:
		if (Count > 1)
		{
			int32 Next = FMath::RandRange(0, Count - 1);
			if (Next == CurrentWaypointIndex)
			{
				Next = (Next + 1) % Count;
			}
			CurrentWaypointIndex = Next;
		}
		break;

	default:
		break;
	}
}

void UAI_PatrolComponent::PushGoalToBlackboard(const FVector& WorldGoal)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (IDP_BlackboardProvider* Board = ResolveBlackboardProvider())
	{
		Board->SetVector(BlackboardKey_PatrolGoal, WorldGoal);
	}
}

void UAI_PatrolComponent::HandlePerceptUpdated(const FAI_Percept& Percept)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	// A strong, currently-sensed percept: engage (push target to the brain) and investigate its location.
	if (Percept.bSensed && Percept.Strength >= InvestigatePerceptThreshold)
	{
		// Hand the target to the brain so combat behaviour takes over.
		if (AActor* Owner = GetOwner())
		{
			if (Percept.SourceId.IsValid())
			{
				if (Owner->GetClass()->ImplementsInterface(UAI_Brain::StaticClass()))
				{
					if (IAI_Brain* Brain = Cast<IAI_Brain>(Owner))
					{
						Brain->SetTargetEntity(Percept.SourceId);
					}
				}
				else if (UActorComponent* BrainComp = Owner->FindComponentByInterface(UAI_Brain::StaticClass()))
				{
					if (IAI_Brain* Brain = Cast<IAI_Brain>(BrainComp))
					{
						Brain->SetTargetEntity(Percept.SourceId);
					}
				}
			}
		}
		InvestigateLocation(Percept.LastKnownLocation);
	}
}

void UAI_PatrolComponent::OnRep_Waypoint()
{
	// Cosmetic only: clients can use CurrentWaypointIndex for route-progress interpolation/UI.
}

FSeam_EntityId UAI_PatrolComponent::GetOwnerEntityId() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FSeam_EntityId::Invalid();
	}
	if (Owner->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Owner);
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Comp);
	}
	return FSeam_EntityId::Invalid();
}

UAI_PerceptionComponent* UAI_PatrolComponent::ResolvePerception() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UAI_PerceptionComponent>() : nullptr;
}

IDP_BlackboardProvider* UAI_PatrolComponent::ResolveBlackboardProvider() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (Owner->GetClass()->ImplementsInterface(UDP_BlackboardProvider::StaticClass()))
	{
		return Cast<IDP_BlackboardProvider>(Owner);
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(UDP_BlackboardProvider::StaticClass()))
	{
		return Cast<IDP_BlackboardProvider>(Comp);
	}
	return nullptr;
}

bool UAI_PatrolComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
