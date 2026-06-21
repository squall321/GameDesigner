// Copyright DesignPatterns plugin. All Rights Reserved.

#include "State/Move_TraversalStates.h"
#include "Component/Move_MovementComponent.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Trace/Move_TraceLibrary.h"
#include "Move_NativeTags.h"

#include "FSM/DPBlackboard.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/DPLog.h"

// ============================ Traversal base (Mantle/Vault) ============================

void UMove_State_TraversalBase::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!Move || !CMC || !BB)
	{
		return;
	}

	// Freeze normal movement during the interpolation and disable collision-driven sliding.
	CMC->SetMovementMode(MOVE_Flying);
	CMC->StopMovementImmediately();
	CMC->GravityScale = 0.f;

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();
	float Duration = Settings ? Settings->FallbackTraversalDuration : 0.45f;
	(void)Profile; // duration is genre-neutral; profiles could extend this later.

	BB->SetVector(Key_TraversalStartLocation, Move->GetCharacter() ? Move->GetCharacter()->GetActorLocation() : FVector::ZeroVector);
	BB->SetFloat(Key_TraversalStartTime, GetWorldTime(OwningComponent));
	BB->SetFloat(Key_TraversalDuration, Duration);
	// The intent component sets the loose Move.Status.Traversing tag when it stamps the request, so it
	// will not queue a new special move while this interpolation runs; this state only owns the motion.
}

void UMove_State_TraversalBase::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	ACharacter* Character = Move ? Move->GetCharacter() : nullptr;
	if (!Move || !BB || !Character)
	{
		return;
	}

	const float Start = BB->GetFloat(Key_TraversalStartTime, 0.f);
	const float Duration = FMath::Max(BB->GetFloat(Key_TraversalDuration, 0.45f), KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((GetWorldTime(OwningComponent) - Start) / Duration, 0.f, 1.f);

	const FVector StartLoc = BB->GetVector(Key_TraversalStartLocation);
	const FVector TargetLoc = BB->GetVector(Key_TraversalTargetLocation);

	// Smooth ease so the pull-up feels weighted; both server and client run this from the same target.
	const float Eased = FMath::SmoothStep(0.f, 1.f, Alpha);
	const FVector NewLoc = FMath::Lerp(StartLoc, TargetLoc, Eased);
	Character->SetActorLocation(NewLoc, /*bSweep*/false);
	// The authored transition (TraversalComplete guard) returns control to a ground state at Alpha==1.
}

void UMove_State_TraversalBase::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		CMC->GravityScale = 1.f;
		CMC->SetMovementMode(MOVE_Walking);
	}
	if (UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		BB->ClearKey(Key_TraversalStartTime);
		BB->ClearKey(Key_TraversalStartLocation);
		BB->ClearKey(Key_TraversalTargetLocation);
		BB->ClearKey(Key_TraversalDuration);
	}
	Super::OnExit_Implementation(OwningComponent);
}

bool UMove_State_TraversalBase::IsTraversalComplete(UDP_StateMachineComponent* OwningComponent) const
{
	const UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!BB)
	{
		return true;
	}
	const float Start = BB->GetFloat(Key_TraversalStartTime, 0.f);
	const float Duration = FMath::Max(BB->GetFloat(Key_TraversalDuration, 0.45f), KINDA_SMALL_NUMBER);
	return (GetWorldTime(OwningComponent) - Start) >= Duration;
}

UMove_State_Mantle::UMove_State_Mantle()
{
	StateTag = MoveNativeTags::State_Mantle;
	MotionConfig.Domain = EMove_MotionDomain::Custom;
	MotionConfig.bOrientToMovement = false;
}

UMove_State_Vault::UMove_State_Vault()
{
	StateTag = MoveNativeTags::State_Vault;
	MotionConfig.Domain = EMove_MotionDomain::Custom;
	MotionConfig.bOrientToMovement = false;
}

// ============================ WallRun ============================

UMove_State_WallRun::UMove_State_WallRun()
{
	StateTag = MoveNativeTags::State_WallRun;
	MotionConfig.Domain = EMove_MotionDomain::Custom;
	MotionConfig.bOrientToMovement = false;
}

void UMove_State_WallRun::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!Move || !CMC || !BB)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();

	float Duration = Settings ? Settings->FallbackWallRunDuration : 1.5f;
	if (Profile && Profile->WallRunDuration > 0.f)
	{
		Duration = Profile->WallRunDuration;
	}
	float DetectDist = Settings ? Settings->FallbackWallDetectDistance : 70.f;
	if (Profile && Profile->WallDetectDistance > 0.f)
	{
		DetectDist = Profile->WallDetectDistance;
	}

	BB->SetFloat(Key_PhaseEndTime, GetWorldTime(OwningComponent) + Duration);

	const TArray<TEnumAsByte<ECollisionChannel>> Channels = Profile ? Profile->TraversalTraceChannels
		: TArray<TEnumAsByte<ECollisionChannel>>();

	// Pick whichever side currently has a wall; record it for OnTick.
	FMove_WallResult Right = UMove_TraceLibrary::FindWall(Move->GetCharacter(), DetectDist, /*bRightSide*/true, Channels);
	FMove_WallResult Left  = UMove_TraceLibrary::FindWall(Move->GetCharacter(), DetectDist, /*bRightSide*/false, Channels);
	const FMove_WallResult& Chosen = Right.bFound ? Right : Left;
	BB->SetInt(Key_WallRunSide, Chosen.bFound ? Chosen.Side : 0);

	// Reduced-gravity "stick" feel: fly with a gravity bias from the profile.
	CMC->SetMovementMode(MOVE_Flying);
	CMC->GravityScale = Profile ? Profile->WallRunGravityScale : 0.25f;
}

void UMove_State_WallRun::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!Move || !CMC || !BB)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();
	float DetectDist = Settings ? Settings->FallbackWallDetectDistance : 70.f;
	if (Profile && Profile->WallDetectDistance > 0.f)
	{
		DetectDist = Profile->WallDetectDistance;
	}
	const int32 Side = BB->GetInt(Key_WallRunSide, 0);
	const TArray<TEnumAsByte<ECollisionChannel>> Channels = Profile ? Profile->TraversalTraceChannels
		: TArray<TEnumAsByte<ECollisionChannel>>();

	// Re-confirm the wall; if lost, the authored WallRun->Jump/Walk transition takes over (guard checks side==0).
	FMove_WallResult Wall = UMove_TraceLibrary::FindWall(Move->GetCharacter(), DetectDist, Side > 0, Channels);
	if (!Wall.bFound)
	{
		BB->SetInt(Key_WallRunSide, 0);
		return;
	}

	// Bias velocity to run along the wall (project current velocity onto the wall plane).
	FVector AlongWall = FVector::VectorPlaneProject(CMC->Velocity, Wall.WallNormal);
	AlongWall.Z = FMath::Max(AlongWall.Z, 0.f); // do not let the wall-run dive.
	CMC->Velocity = AlongWall;
}

void UMove_State_WallRun::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		CMC->GravityScale = 1.f;
		CMC->SetMovementMode(MOVE_Falling);
	}
	if (UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		BB->ClearKey(Key_PhaseEndTime);
		BB->ClearKey(Key_WallRunSide);
	}
	Super::OnExit_Implementation(OwningComponent);
}

// ============================ Climb ============================

UMove_State_Climb::UMove_State_Climb()
{
	StateTag = MoveNativeTags::State_Climb;
	MotionConfig.Domain = EMove_MotionDomain::Custom;
	MotionConfig.bOrientToMovement = false;
}

void UMove_State_Climb::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		CMC->SetMovementMode(MOVE_Flying);
		CMC->GravityScale = 0.f;
		CMC->StopMovementImmediately();
	}
}

void UMove_State_Climb::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	if (!Move || !CMC)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();
	float ClimbSpeed = Profile && Profile->ClimbSpeed > 0.f ? Profile->ClimbSpeed
		: (Settings ? Settings->FallbackWalkSpeed : 200.f);
	float DetectDist = Settings ? Settings->FallbackWallDetectDistance : 70.f;
	if (Profile && Profile->WallDetectDistance > 0.f)
	{
		DetectDist = Profile->WallDetectDistance;
	}
	const TArray<TEnumAsByte<ECollisionChannel>> Channels = Profile ? Profile->TraversalTraceChannels
		: TArray<TEnumAsByte<ECollisionChannel>>();

	// A forward trace (use the right-side helper twice would miss a wall directly ahead, so we sample
	// the floor-slope-independent front wall via a small lateral pair and pick whichever is found).
	FMove_WallResult Right = UMove_TraceLibrary::FindWall(Move->GetCharacter(), DetectDist, true, Channels);
	FMove_WallResult Left  = UMove_TraceLibrary::FindWall(Move->GetCharacter(), DetectDist, false, Channels);
	const bool bWall = Right.bFound || Left.bFound;
	if (!bWall)
	{
		// Wall lost: the authored Climb->Jump/Walk transition handles detaching (guard reads this absence).
		CMC->SetMovementMode(MOVE_Falling);
		CMC->GravityScale = 1.f;
		return;
	}

	// Move on the wall plane: up/down from intent.Y projected vertically, lateral from intent.X.
	const FVector Intent = Move->GetMoveIntent();
	const FVector Up = FVector::UpVector;
	const FVector Right3D = Move->GetCharacter() ? Move->GetCharacter()->GetActorRightVector() : FVector::RightVector;
	FVector ClimbVel = (Up * Intent.Y + Right3D * Intent.X);
	ClimbVel = ClimbVel.GetSafeNormal() * ClimbSpeed;
	CMC->Velocity = ClimbVel;
}

void UMove_State_Climb::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		CMC->GravityScale = 1.f;
		CMC->SetMovementMode(MOVE_Walking);
	}
	Super::OnExit_Implementation(OwningComponent);
}
