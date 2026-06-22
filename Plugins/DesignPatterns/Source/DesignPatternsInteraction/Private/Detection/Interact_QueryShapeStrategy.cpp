// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Detection/Interact_QueryShapeStrategy.h"

#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

//~ UInteract_QueryShapeStrategy (base) -------------------------------------------------------

void UInteract_QueryShapeStrategy::GatherCandidateActors_Implementation(
	const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
	int32 /*MaxCandidates*/, TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
	// Base default: gather nothing. Concrete shapes override; a null shape on the component is handled
	// by the component falling back to its private DetectCandidates path (never reaches here).
	OutActors.Reset();
}

const UWorld* UInteract_QueryShapeStrategy::ResolveWorld(const UObject* WorldContext)
{
	return WorldContext ? WorldContext->GetWorld() : nullptr;
}

AActor* UInteract_QueryShapeStrategy::ResolveInstigator(const FInteract_Query& Query)
{
	return Query.Instigator.Get();
}

//~ UInteract_QueryShape_Sphere ---------------------------------------------------------------

void UInteract_QueryShape_Sphere::GatherCandidateActors_Implementation(
	const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
	int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
	OutActors.Reset();

	const UWorld* World = ResolveWorld(WorldContext);
	AActor* Instigator = ResolveInstigator(Query);
	const float Range = FMath::Max(0.f, Params.Range);
	if (!World || Range <= 0.f)
	{
		return;
	}

	FCollisionQueryParams CQP(SCENE_QUERY_STAT(Interact_ShapeSphere), /*bTraceComplex=*/false, Instigator);
	if (Instigator)
	{
		CQP.AddIgnoredActor(Instigator);
	}

	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Range);
	World->OverlapMultiByChannel(Overlaps, Query.ViewLocation, FQuat::Identity, Params.Channel.GetValue(), Sphere, CQP);

	const int32 Cap = FMath::Max(1, MaxCandidates);
	TSet<AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (OutActors.Num() >= Cap)
		{
			break;
		}
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == Instigator || Seen.Contains(HitActor))
		{
			continue;
		}
		Seen.Add(HitActor);
		OutActors.Add(HitActor);
	}
}

//~ UInteract_QueryShape_Line -----------------------------------------------------------------

void UInteract_QueryShape_Line::GatherCandidateActors_Implementation(
	const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
	int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
	OutActors.Reset();

	const UWorld* World = ResolveWorld(WorldContext);
	AActor* Instigator = ResolveInstigator(Query);
	const float Range = FMath::Max(0.f, Params.Range);
	if (!World || Range <= 0.f)
	{
		return;
	}

	const FVector Start = Query.ViewLocation;
	const FVector End = Start + Query.ViewDirection.GetSafeNormal() * Range;

	FCollisionQueryParams CQP(SCENE_QUERY_STAT(Interact_ShapeLine), /*bTraceComplex=*/false, Instigator);
	if (Instigator)
	{
		CQP.AddIgnoredActor(Instigator);
	}

	TArray<FHitResult> Hits;
	const float Radius = FMath::Max(0.f, TraceRadius);
	if (Radius > 0.f)
	{
		World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, Params.Channel.GetValue(),
			FCollisionShape::MakeSphere(Radius), CQP);
	}
	else
	{
		World->LineTraceMultiByChannel(Hits, Start, End, Params.Channel.GetValue(), CQP);
	}

	const int32 Cap = FMath::Max(1, MaxCandidates);
	TSet<AActor*> Seen;
	for (const FHitResult& Hit : Hits)
	{
		if (OutActors.Num() >= Cap)
		{
			break;
		}
		AActor* HitActor = Hit.GetActor();
		if (!HitActor || HitActor == Instigator || Seen.Contains(HitActor))
		{
			continue;
		}
		Seen.Add(HitActor);
		OutActors.Add(HitActor);
	}
}

//~ UInteract_QueryShape_Cone -----------------------------------------------------------------

void UInteract_QueryShape_Cone::GatherCandidateActors_Implementation(
	const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
	int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
	OutActors.Reset();

	const UWorld* World = ResolveWorld(WorldContext);
	AActor* Instigator = ResolveInstigator(Query);
	const float Range = FMath::Max(0.f, Params.Range);
	if (!World || Range <= 0.f)
	{
		return;
	}

	FCollisionQueryParams CQP(SCENE_QUERY_STAT(Interact_ShapeCone), /*bTraceComplex=*/false, Instigator);
	if (Instigator)
	{
		CQP.AddIgnoredActor(Instigator);
	}

	TArray<FOverlapResult> Overlaps;
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(Range);
	World->OverlapMultiByChannel(Overlaps, Query.ViewLocation, FQuat::Identity, Params.Channel.GetValue(), Sphere, CQP);

	const float CosCone = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Params.ConeHalfAngleDeg, 0.f, 180.f)));
	const FVector ViewDir = Query.ViewDirection.GetSafeNormal();

	const int32 Cap = FMath::Max(1, MaxCandidates);
	TSet<AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (OutActors.Num() >= Cap)
		{
			break;
		}
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == Instigator || Seen.Contains(HitActor))
		{
			continue;
		}

		// Cheap angular cull around the view direction (the interactor recomputes exact angle later).
		const FVector ToTarget = HitActor->GetActorLocation() - Query.ViewLocation;
		const float Dist = ToTarget.Size();
		const FVector Dir = (Dist > KINDA_SMALL_NUMBER) ? (ToTarget / Dist) : ViewDir;
		if (FVector::DotProduct(ViewDir, Dir) < CosCone)
		{
			continue;
		}

		Seen.Add(HitActor);
		OutActors.Add(HitActor);
	}
}

//~ UInteract_QueryShape_WidgetUnderCursor ----------------------------------------------------

void UInteract_QueryShape_WidgetUnderCursor::GatherCandidateActors_Implementation(
	const UObject* WorldContext, const FInteract_Query& Query, const FInteract_DetectionParams& Params,
	int32 MaxCandidates, TArray<TWeakObjectPtr<AActor>>& OutActors) const
{
	OutActors.Reset();

	const UWorld* World = ResolveWorld(WorldContext);
	AActor* Instigator = ResolveInstigator(Query);
	if (!World)
	{
		return;
	}

	// Resolve the LOCAL player controller from the instigator pawn (cursor only exists on the client).
	APlayerController* PC = nullptr;
	if (const APawn* Pawn = Cast<APawn>(Instigator))
	{
		PC = Cast<APlayerController>(Pawn->GetController());
	}
	if (!PC || !PC->IsLocalController())
	{
		// No local cursor here (dedicated server / remote proxy): gather nothing. Server authorisation
		// flows through ServerInteractAt, which re-validates, so this is safe.
		return;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDir = FVector::ForwardVector;
	if (!PC->DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
	{
		return;
	}

	const float Distance = FMath::Max(0.f, CursorTraceDistance);
	const FVector End = WorldOrigin + WorldDir.GetSafeNormal() * Distance;

	FCollisionQueryParams CQP(SCENE_QUERY_STAT(Interact_ShapeCursor), /*bTraceComplex=*/false, Instigator);
	if (Instigator)
	{
		CQP.AddIgnoredActor(Instigator);
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, WorldOrigin, End, Params.Channel.GetValue(), CQP))
	{
		if (AActor* HitActor = Hit.GetActor())
		{
			if (HitActor != Instigator)
			{
				OutActors.Add(HitActor);
			}
		}
	}
}
