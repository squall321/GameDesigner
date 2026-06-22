// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Collision/Cam_CameraCollisionProbe.h"

#include "Core/DPLog.h"

#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "GameFramework/Actor.h"

UCam_CameraCollisionProbe::UCam_CameraCollisionProbe()
{
}

void UCam_CameraCollisionProbe::ResolveCollision_Implementation(const UObject* /*WorldContext*/, const FCam_ViewContext& /*Context*/,
	const FCam_CameraView& DesiredView, float /*DeltaTime*/,
	FCam_CameraView& OutAdjustedView, FCam_CollisionResult& OutResult)
{
	// Base behaviour: pass the desired view through unchanged (a "no collision" probe).
	OutAdjustedView = DesiredView;
	OutResult = FCam_CollisionResult();
}

// ---------------------------------------------------------------------------------------------------

UCam_SweptSphereCollisionProbe::UCam_SweptSphereCollisionProbe()
{
}

void UCam_SweptSphereCollisionProbe::ResetProbeState()
{
	SmoothedDistance = 0.f;
	SmoothedOcclusion = 0.f;
	bSeeded = false;
}

void UCam_SweptSphereCollisionProbe::ResolveCollision_Implementation(const UObject* WorldContext, const FCam_ViewContext& Context,
	const FCam_CameraView& DesiredView, float DeltaTime,
	FCam_CameraView& OutAdjustedView, FCam_CollisionResult& OutResult)
{
	// Default to the desired view; we only ever move the camera radially in toward the pivot.
	OutAdjustedView = DesiredView;
	OutResult = FCam_CollisionResult();

	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		// No world to trace against: pass through unchanged but keep smoothing seeded sensibly.
		return;
	}

	const FVector Pivot = Context.PivotLocation;
	const FVector Desired = DesiredView.Location;

	const FVector ToCamera = Desired - Pivot;
	const float DesiredDistance = ToCamera.Size();
	if (DesiredDistance <= KINDA_SMALL_NUMBER)
	{
		// Camera sits on the pivot (e.g. first person): nothing to pull in; clear occlusion.
		SmoothedDistance = DesiredDistance;
		SmoothedOcclusion = 0.f;
		bSeeded = true;
		return;
	}

	const FVector Dir = ToCamera / DesiredDistance;

	// Exclude the view target (and its attachment hierarchy) from the sweep so we don't pull in on the
	// very actor we're framing.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Cam_CollisionProbe), /*bTraceComplex=*/false);
	if (const AActor* TargetActor = Context.ViewTarget.Get())
	{
		Params.AddIgnoredActor(TargetActor);
	}

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(FMath::Max(ProbeRadius, 0.f));

	// --- Pull-in sweep: pivot -> desired camera location. ---
	float TargetDistance = DesiredDistance;
	FHitResult Hit;
	const bool bBlocked = World->SweepSingleByChannel(
		Hit, Pivot, Desired, FQuat::Identity, ProbeChannel.GetValue(), Sphere, Params);

	if (bBlocked)
	{
		// Pull the camera in to the blocking hit, clamped so it never enters the pivot.
		const float HitDistance = FMath::Max(Hit.Distance, MinPullInDistance);
		TargetDistance = FMath::Min(DesiredDistance, HitDistance);
		OutResult.HitFraction = (Hit.Time >= 0.f) ? Hit.Time : (TargetDistance / DesiredDistance);
	}

	// Seed the smoothed distance on the first valid frame so the camera doesn't jump in from infinity.
	if (!bSeeded)
	{
		SmoothedDistance = TargetDistance;
		bSeeded = true;
	}
	else
	{
		// Asymmetric lag: pulling IN (distance shrinks) uses the fast lag; easing OUT uses the slow lag.
		const bool bPullingIn = TargetDistance < SmoothedDistance;
		const float Lag = bPullingIn ? PullInLag : PullOutLag;
		if (Lag <= 0.f || DeltaTime <= 0.f)
		{
			SmoothedDistance = TargetDistance;
		}
		else
		{
			// Frame-rate-independent exponential approach toward TargetDistance.
			const float Alpha = 1.f - FMath::Exp(-DeltaTime / Lag);
			SmoothedDistance = FMath::Lerp(SmoothedDistance, TargetDistance, Alpha);
		}
	}

	OutAdjustedView.Location = Pivot + Dir * SmoothedDistance;
	OutResult.bCameraPulledIn = SmoothedDistance < (DesiredDistance - KINDA_SMALL_NUMBER);

	// --- Occlusion test: pivot -> (smoothed) camera location, line trace. ---
	if (bComputeOcclusion)
	{
		FHitResult OccHit;
		const bool bOccluded = World->LineTraceSingleByChannel(
			OccHit, Pivot, OutAdjustedView.Location, ProbeChannel.GetValue(), Params);

		const float OccTarget = bOccluded ? 1.f : 0.f;
		if (OcclusionFullSeconds <= 0.f || DeltaTime <= 0.f)
		{
			SmoothedOcclusion = OccTarget;
		}
		else
		{
			// Move toward 0/1 at a rate that fully transitions in OcclusionFullSeconds.
			const float Step = DeltaTime / OcclusionFullSeconds;
			SmoothedOcclusion = FMath::Clamp(
				SmoothedOcclusion + FMath::Sign(OccTarget - SmoothedOcclusion) * Step, 0.f, 1.f);
		}

		OutResult.bTargetOccluded = bOccluded;
		OutResult.OcclusionAlpha = SmoothedOcclusion;
	}

	UE_LOG(LogDP, VeryVerbose, TEXT("[Camera] Probe dist=%.1f/%.1f pulledIn=%d occ=%.2f"),
		SmoothedDistance, DesiredDistance, OutResult.bCameraPulledIn, OutResult.OcclusionAlpha);
}
