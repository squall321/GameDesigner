// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_SplineRailMode.h"

#include "Core/DPLog.h"

#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

UCam_SplineRailMode::UCam_SplineRailMode()
{
}

void UCam_SplineRailMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	Super::OnEnterStack_Implementation(Context);

	bSeeded = false;
	SmoothedDistance = 0.f;
	SmoothedRotation = Context.PreviousCameraRotation;

	// Resolve the rail eagerly so a missing rail is logged at push time, not silently each tick.
	if (const AActor* Target = Context.ViewTarget.Get())
	{
		ResolveRail(Target);
	}
}

void UCam_SplineRailMode::OnExitStack_Implementation()
{
	Rail.Reset();
	Super::OnExitStack_Implementation();
}

USplineComponent* UCam_SplineRailMode::ResolveRail(const UObject* WorldContext)
{
	if (USplineComponent* Existing = Rail.Get())
	{
		return Existing;
	}

	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || RailActorTag.IsNone())
	{
		return nullptr;
	}

	// Find the first actor carrying RailActorTag and take its spline component.
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->ActorHasTag(RailActorTag))
		{
			if (USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>())
			{
				Rail = Spline;
				UE_LOG(LogDP, Verbose, TEXT("[Camera] SplineRail resolved rail on %s."), *GetNameSafe(Actor));
				return Spline;
			}
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[Camera] SplineRail found no actor tagged '%s' with a spline."), *RailActorTag.ToString());
	return nullptr;
}

void UCam_SplineRailMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	const FVector Pivot = Context.PivotLocation;
	const FVector LookAt = Pivot + LookAtOffset;

	USplineComponent* Spline = ResolveRail(Context.ViewTarget.Get());
	if (!Spline)
	{
		// Graceful fallback: sit above the pivot looking down at it so the mode is never broken.
		OutView.Location = Pivot + FVector(0.f, 0.f, FallbackHeight);
		OutView.Rotation = (LookAt - OutView.Location).Rotation();
		OutView.FOV = FieldOfView;
		return;
	}

	// Find the input key (continuous spline parameter) nearest the pivot — this is where on the rail the
	// camera should sit to "track" the subject.
	const float TargetKey = Spline->FindInputKeyClosestToWorldLocation(Pivot);

	if (!bSeeded)
	{
		SmoothedDistance = TargetKey;
		bSeeded = true;
	}
	else if (DistanceLag > 0.f && DeltaTime > 0.f)
	{
		const float Alpha = 1.f - FMath::Exp(-DeltaTime / DistanceLag);
		SmoothedDistance = FMath::Lerp(SmoothedDistance, TargetKey, Alpha);
	}
	else
	{
		SmoothedDistance = TargetKey;
	}

	const FVector RailLocation = Spline->GetLocationAtSplineInputKey(SmoothedDistance, ESplineCoordinateSpace::World);

	// Look direction: aim at the subject, or follow the spline tangent.
	FRotator TargetRotation;
	if (bLookAtPivot)
	{
		TargetRotation = (LookAt - RailLocation).Rotation();
	}
	else
	{
		const FVector Tangent = Spline->GetTangentAtSplineInputKey(SmoothedDistance, ESplineCoordinateSpace::World);
		TargetRotation = Tangent.IsNearlyZero() ? (LookAt - RailLocation).Rotation() : Tangent.Rotation();
	}

	SmoothedRotation = SmoothRotator(SmoothedRotation, TargetRotation, RotationLag, DeltaTime);

	OutView.Location = RailLocation;
	OutView.Rotation = SmoothedRotation;
	OutView.FOV = FieldOfView;
}
