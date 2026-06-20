// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_SteeringComponent.h"
#include "Crowd/SimAg_Locomotion.h"
#include "Crowd/SimAg_FlowField.h"
#include "Crowd/SimAg_FlowFieldSubsystem.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Brain/SimAg_BrainTypes.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

USimAg_SteeringComponent::USimAg_SteeringComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Steering itself is not replicated; the move GOAL is authority-derived and the math runs locally.
	SetIsReplicatedByDefault(false);
}

void USimAg_SteeringComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		SteeringPeriod = 1.f / FMath::Max(1.f, Settings->SteeringTickHz);
		ArrivalRadius = FMath::Max(1.f, Settings->ArrivalRadius);
		SeparationWeight = FMath::Max(0.f, Settings->SeparationWeight);
		if (SeparationRadius <= 0.f)
		{
			SeparationRadius = FMath::Max(1.f, Settings->DefaultSeparationRadius);
		}
	}

	ResolveLocomotion();

	// Register with the flow-field subsystem so this agent participates in neighbour separation.
	if (USimAg_FlowFieldSubsystem* Sub =
		FDP_SubsystemStatics::GetWorldSubsystem<USimAg_FlowFieldSubsystem>(this))
	{
		FlowField = Sub;
		Sub->RegisterAgent(this);
	}

	// Stagger steering recomputes across a crowd.
	SteeringAccumulator = FMath::FRand() * SteeringPeriod;
}

void USimAg_SteeringComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (USimAg_FlowFieldSubsystem* Sub = FlowField.Get())
	{
		Sub->UnregisterAgent(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool USimAg_SteeringComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && (Owner->GetLocalRole() == ROLE_Authority);
}

FVector USimAg_SteeringComponent::GetAgentLocation() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void USimAg_SteeringComponent::ResolveLocomotion()
{
	Locomotion = TScriptInterface<ISimAg_Locomotion>();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	auto Adopt = [this](UObject* Candidate) -> bool
	{
		if (Candidate && Candidate->Implements<USimAg_Locomotion>())
		{
			Locomotion.SetObject(Candidate);
			Locomotion.SetInterface(Cast<ISimAg_Locomotion>(Candidate));
			return true;
		}
		return false;
	};

	// Prefer a locomotion implementer on the owning actor; otherwise a sibling component; otherwise the
	// controller (which may be an AIController implementing the seam). Never hard-assumes AAIController.
	if (Adopt(Owner))
	{
		return;
	}
	for (UActorComponent* Comp : Owner->GetComponents())
	{
		if (Adopt(Comp))
		{
			return;
		}
	}
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (Adopt(Pawn->GetController()))
		{
			return;
		}
	}
}

TScriptInterface<IDP_BlackboardProvider> USimAg_SteeringComponent::ResolveBlackboard() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
		{
			return Agent->GetBlackboardProvider();
		}
	}
	return TScriptInterface<IDP_BlackboardProvider>();
}

void USimAg_SteeringComponent::SetMoveTarget(const FVector& WorldGoal)
{
	// AUTHORITY GUARD at top: the goal is always server-derived.
	if (!HasOwnerAuthority())
	{
		return;
	}
	MoveTarget = WorldGoal;
	bHasTarget = true;

	// Let a path-following substrate plan immediately.
	if (Locomotion)
	{
		ISimAg_Locomotion::Execute_RequestMoveTo(Locomotion.GetObject(), WorldGoal);
	}
}

void USimAg_SteeringComponent::ClearMoveTarget()
{
	if (!HasOwnerAuthority())
	{
		return;
	}
	bHasTarget = false;
	if (TScriptInterface<IDP_BlackboardProvider> BB = ResolveBlackboard())
	{
		BB->SetBool(MovingKey, false);
	}
}

void USimAg_SteeringComponent::SyncTargetFromBlackboard()
{
	// Authority mirrors the brain's chosen MoveTarget into the steering goal.
	if (!HasOwnerAuthority())
	{
		return;
	}
	TScriptInterface<IDP_BlackboardProvider> BB = ResolveBlackboard();
	if (!BB)
	{
		return;
	}
	const bool bWantsMove = BB->HasKey(MovingKey) ? BB->GetBool(MovingKey, false) : BB->HasKey(MoveTargetKey);
	if (bWantsMove && BB->HasKey(MoveTargetKey))
	{
		const FVector Goal = BB->GetVector(MoveTargetKey);
		if (!bHasTarget || !Goal.Equals(MoveTarget, 1.f))
		{
			SetMoveTarget(Goal);
		}
	}
	else if (!bWantsMove && bHasTarget)
	{
		ClearMoveTarget();
	}
}

void USimAg_SteeringComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Authority keeps the goal in sync with the brain; the steering MATH runs everywhere so movement is
	// smooth on clients too (clients read the goal that arrived via the agent/locomotion substrate).
	SyncTargetFromBlackboard();

	if (!bHasTarget)
	{
		return;
	}

	SteeringAccumulator += DeltaTime;
	if (SteeringAccumulator >= SteeringPeriod)
	{
		SteeringAccumulator = 0.f;
		StepSteering();
	}
}

void USimAg_SteeringComponent::StepSteering()
{
	const FVector Location = GetAgentLocation();
	const float DistToGoal = static_cast<float>(FVector::Dist(Location, MoveTarget));

	// Arrived: stop issuing movement input and (on authority) clear the moving flag.
	if (DistToGoal <= ArrivalRadius)
	{
		ApplyDesiredVelocity(FVector::ZeroVector);
		if (HasOwnerAuthority())
		{
			bHasTarget = false;
			if (TScriptInterface<IDP_BlackboardProvider> BB = ResolveBlackboard())
			{
				BB->SetBool(MovingKey, false);
			}
		}
		return;
	}

	// Goal-seeking direction: prefer flow-field guidance, else straight at the goal.
	FVector FlowDir = FVector::ZeroVector;
	FVector Separation = FVector::ZeroVector;
	if (USimAg_FlowFieldSubsystem* Sub = FlowField.Get())
	{
		FlowDir = ISimAg_FlowField::Execute_SampleFlowDirection(Sub, Location, MoveTarget);
		Separation = ISimAg_FlowField::Execute_SampleSeparation(Sub, Location, SeparationRadius);
	}
	if (FlowDir.IsNearlyZero())
	{
		FlowDir = (MoveTarget - Location).GetSafeNormal();
	}

	// Blend goal-seeking with crowd separation by the designer-authored weight. Result is a unit-ish
	// desired velocity (magnitude in [0,1] of max speed, per the locomotion seam convention).
	FVector Desired = FlowDir + (Separation * SeparationWeight);
	if (!Desired.IsNearlyZero())
	{
		Desired = Desired.GetSafeNormal();
	}
	ApplyDesiredVelocity(Desired);
}

void USimAg_SteeringComponent::ApplyDesiredVelocity(const FVector& WorldDesiredVelocity)
{
	// Re-resolve locomotion if it went stale (controller possessed/unpossessed, component replaced).
	if (!Locomotion)
	{
		ResolveLocomotion();
	}

	if (Locomotion)
	{
		ISimAg_Locomotion::Execute_SetMovementInput(Locomotion.GetObject(), WorldDesiredVelocity);
		return;
	}

	// Fallback for a bare actor with no locomotion substrate: nudge the root transform directly so the
	// component is still functional. Uses the steering period as the integration step.
	if (WorldDesiredVelocity.IsNearlyZero())
	{
		return;
	}
	if (AActor* Owner = GetOwner())
	{
		// A modest fallback speed derived from arrival radius keeps the nudge frame-rate-independent and
		// free of magic constants: cover the arrival radius over roughly one second of steering steps.
		const float FallbackSpeed = ArrivalRadius;
		const FVector Delta = WorldDesiredVelocity * FallbackSpeed * SteeringPeriod;
		Owner->AddActorWorldOffset(Delta, /*bSweep*/ true);
	}
}
