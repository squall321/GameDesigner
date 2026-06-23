// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Nav/SimAg_NavLocomotion.h"
#include "Nav/SimAg_PathCacheSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Async/Async.h"
#include "NavigationData.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"

USimAg_NavLocomotion::USimAg_NavLocomotion()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USimAg_NavLocomotion::BeginPlay()
{
	Super::BeginPlay();
}

void USimAg_NavLocomotion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Cancel any in-flight path request so its callback can't fire into a destroyed component.
	if (ActivePathRequestId != 0)
	{
		if (USimAg_PathCacheSubsystem* Cache = GetPathCache())
		{
			Cache->CancelRequest(ActivePathRequestId);
		}
		ActivePathRequestId = 0;
	}
	CurrentPath.Reset();
	Super::EndPlay(EndPlayReason);
}

void USimAg_NavLocomotion::RequestMoveTo_Implementation(const FVector& WorldGoal)
{
	PendingGoal = WorldGoal;
	bHasGoal = true;

	const AActor* Owner = GetOwner();
	const FVector Start = Owner ? Owner->GetActorLocation() : WorldGoal;

	USimAg_PathCacheSubsystem* Cache = GetPathCache();
	if (!Cache)
	{
		// No nav available: fall back to steering straight at the goal (no path).
		CurrentPath.Reset();
		CurrentPathPoint = 0;
		return;
	}

	// Supersede any prior request.
	if (ActivePathRequestId != 0)
	{
		Cache->CancelRequest(ActivePathRequestId);
		ActivePathRequestId = 0;
	}

	// Bind a weak-this guarded callback so a destroyed component never dereferences.
	TWeakObjectPtr<USimAg_NavLocomotion> WeakThis(this);
	FSimAg_OnPathReady OnReady;
	OnReady.BindLambda([WeakThis](FNavPathSharedPtr Path)
	{
		// The path-cache fans this delegate out from OnNavPathReady, which runs on whatever thread the
		// engine async pathfind completes on (a nav worker thread, not the game thread). HandlePathReady
		// advances the path cursor and mutates component state, so marshal it back to the game thread.
		if (USimAg_NavLocomotion* Strong = WeakThis.Get())
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, Path]()
			{
				if (USimAg_NavLocomotion* GameThreadStrong = WeakThis.Get())
				{
					GameThreadStrong->HandlePathReady(Path);
				}
			});
		}
	});

	ActivePathRequestId = Cache->RequestPathAsync(Start, WorldGoal, OnReady);
}

void USimAg_NavLocomotion::HandlePathReady(FNavPathSharedPtr Path)
{
	ActivePathRequestId = 0;
	if (Path.IsValid() && Path->IsValid())
	{
		CurrentPath = Path;
		CurrentPathPoint = 1; // index 0 is the start; head toward the first real leg
		AdvancePathCursor();
	}
	else
	{
		// Even a failed/partial-empty path leaves us steering straight at the goal.
		CurrentPath.Reset();
		CurrentPathPoint = 0;
	}
}

void USimAg_NavLocomotion::SetMovementInput_Implementation(const FVector& WorldDesiredVelocity)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Keep the path cursor up to date so GetNextPathPoint returns the right leg.
	AdvancePathCursor();

	FVector Input = WorldDesiredVelocity;
	Input.Z = 0.f;
	if (Input.IsNearlyZero())
	{
		return;
	}

	// Prefer a Character's movement component (proper nav-aware movement, handles off-mesh links).
	if (APawn* Pawn = Cast<APawn>(Owner))
	{
		const FVector Dir = Input.GetSafeNormal();
		const float Scale = FMath::Clamp(static_cast<float>(Input.Size()), 0.f, 1.f);
		Pawn->AddMovementInput(Dir, Scale);
		return;
	}

	// Bare actor fallback: nudge the root transform directly so the component is still useful.
	if (UCharacterMovementComponent* Move = Owner->FindComponentByClass<UCharacterMovementComponent>())
	{
		Move->AddInputVector(Input);
		return;
	}

	const float DeltaTime = Owner->GetWorld() ? Owner->GetWorld()->GetDeltaSeconds() : 0.f;
	Owner->AddActorWorldOffset(Input * DeltaTime, /*bSweep*/ true);
}

FVector USimAg_NavLocomotion::GetNextPathPoint() const
{
	if (CurrentPath.IsValid() && CurrentPath->IsValid())
	{
		const TArray<FNavPathPoint>& Points = CurrentPath->GetPathPoints();
		if (Points.IsValidIndex(CurrentPathPoint))
		{
			return Points[CurrentPathPoint].Location;
		}
	}
	return bHasGoal ? PendingGoal : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}

bool USimAg_NavLocomotion::IsPathPartial() const
{
	return CurrentPath.IsValid() && CurrentPath->IsValid() && CurrentPath->IsPartial();
}

bool USimAg_NavLocomotion::HasPath() const
{
	return CurrentPath.IsValid() && CurrentPath->IsValid();
}

void USimAg_NavLocomotion::AdvancePathCursor()
{
	if (!CurrentPath.IsValid() || !CurrentPath->IsValid())
	{
		return;
	}
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const FVector AgentLoc = Owner->GetActorLocation();
	const TArray<FNavPathPoint>& Points = CurrentPath->GetPathPoints();

	// Skip past points the agent has effectively reached (handles off-mesh link endpoints too, which are
	// just points along the engine path).
	while (Points.IsValidIndex(CurrentPathPoint))
	{
		const float DistSq = static_cast<float>(FVector::DistSquared(AgentLoc, Points[CurrentPathPoint].Location));
		if (DistSq <= PathPointAcceptRadius * PathPointAcceptRadius)
		{
			++CurrentPathPoint;
		}
		else
		{
			break;
		}
	}
}

USimAg_PathCacheSubsystem* USimAg_NavLocomotion::GetPathCache() const
{
	if (CachedPathCache.IsValid())
	{
		return CachedPathCache.Get();
	}
	USimAg_PathCacheSubsystem* Cache = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_PathCacheSubsystem>(this);
	const_cast<USimAg_NavLocomotion*>(this)->CachedPathCache = Cache;
	return Cache;
}
