// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_CinematicMode.h"
#include "Seam/Cam_TargetSource.h"
#include "Cam_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Identity/Seam_EntityId.h"
#include "Identity/Seam_EntityIdentity.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

UCam_CinematicMode::UCam_CinematicMode()
{
	SmoothedFOV = Framing.FieldOfView;
}

void UCam_CinematicMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	Super::OnEnterStack_Implementation(Context);
	bSeeded = false;
	SmoothedLocation = Context.PreviousCameraLocation;
	SmoothedRotation = Context.PreviousCameraRotation;
	SmoothedFOV = Framing.FieldOfView;
}

void UCam_CinematicMode::OnExitStack_Implementation()
{
	TargetSourceCache = nullptr;
	Super::OnExitStack_Implementation();
}

TScriptInterface<ICam_TargetSource> UCam_CinematicMode::ResolveTargetSource(const UObject* WorldContext)
{
	// Re-use a still-valid cached seam.
	if (TargetSourceCache.GetObject() && TargetSourceCache.GetInterface())
	{
		return TargetSourceCache;
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext);
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(Cam_NativeTags::Service_TargetSource);
	if (Provider && Provider->GetClass()->ImplementsInterface(UCam_TargetSource::StaticClass()))
	{
		TScriptInterface<ICam_TargetSource> Result;
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ICam_TargetSource>(Provider));
		TargetSourceCache = Result;
		return Result;
	}
	return nullptr;
}

bool UCam_CinematicMode::TryGetLockedTargetLocation(const UObject* WorldContext, FVector& OutLocation)
{
	TScriptInterface<ICam_TargetSource> Source = ResolveTargetSource(WorldContext);
	if (!Source || !Source.GetObject())
	{
		return false;
	}

	UObject* SourceObj = Source.GetObject();
	if (!ICam_TargetSource::Execute_HasTarget(SourceObj))
	{
		return false;
	}

	const FSeam_EntityId TargetId = ICam_TargetSource::Execute_GetCurrentTarget(SourceObj);
	if (!TargetId.IsValid())
	{
		return false;
	}

	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}

	// Resolve the id to a world location by matching against actors that expose ISeam_EntityIdentity.
	// We never keep the pointer — we read its location for this frame only.
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !Actor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
		{
			continue;
		}
		const FSeam_EntityId Id = ISeam_EntityIdentity::Execute_GetEntityId(Actor);
		if (Id == TargetId)
		{
			OutLocation = Actor->GetActorLocation();
			return true;
		}
	}
	return false;
}

void UCam_CinematicMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	const FVector Pivot = Context.PivotLocation;

	// Build the desired (instantaneous) framed view from the solver.
	FCam_CameraView Desired;
	FVector LockedLoc;
	if (bDualTargetFrame && TryGetLockedTargetLocation(Context.ViewTarget.Get(), LockedLoc))
	{
		Desired = FCam_FramingSolver::SolveDual(Pivot, LockedLoc, Context.ControlRotation, Framing);
	}
	else
	{
		Desired = FCam_FramingSolver::SolveSingle(Pivot, Context.ControlRotation, Framing);
	}

	// Seed on the first frame so the camera doesn't glide in from the previous mode's POV abruptly.
	if (!bSeeded)
	{
		SmoothedLocation = Desired.Location;
		SmoothedRotation = Desired.Rotation;
		SmoothedFOV = Desired.FOV;
		bSeeded = true;
	}
	else
	{
		SmoothedLocation = SmoothVector(SmoothedLocation, Desired.Location, LocationLag, DeltaTime);
		SmoothedRotation = SmoothRotator(SmoothedRotation, Desired.Rotation, RotationLag, DeltaTime);
		// Ease FOV with the same rotation lag so dolly-zoom transitions feel coupled to the aim.
		if (RotationLag > 0.f && DeltaTime > 0.f)
		{
			const float Alpha = 1.f - FMath::Exp(-DeltaTime / RotationLag);
			SmoothedFOV = FMath::Lerp(SmoothedFOV, Desired.FOV, Alpha);
		}
		else
		{
			SmoothedFOV = Desired.FOV;
		}
	}

	OutView.Location = SmoothedLocation;
	OutView.Rotation = SmoothedRotation;
	OutView.FOV = SmoothedFOV;
}

// ---------------------------------------------------------------------------------------------------

UCam_CinematicOverrideMode::UCam_CinematicOverrideMode()
{
}

void UCam_CinematicOverrideMode::SetOverride(const FTransform& InPOV, float InFOV)
{
	OverrideLocation = InPOV.GetLocation();
	OverrideRotation = InPOV.GetRotation().Rotator();
	OverrideFOV = InFOV;
	bHasOverride = true;
}

void UCam_CinematicOverrideMode::ConfigureBlend(float InBlendIn, float InBlendOut)
{
	BlendInTime = FMath::Max(InBlendIn, 0.f);
	BlendOutTime = FMath::Max(InBlendOut, 0.f);
}

void UCam_CinematicOverrideMode::UpdateView_Implementation(float /*DeltaTime*/, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	if (!bHasOverride)
	{
		// Not yet driven: report the live camera so a just-pushed override doesn't snap to the origin.
		OutView.Location = Context.PreviousCameraLocation;
		OutView.Rotation = Context.PreviousCameraRotation;
		return;
	}

	OutView.Location = OverrideLocation;
	OutView.Rotation = OverrideRotation;
	// FOV <= 0 means "keep contextual FOV": leave OutView.FOV at its default/incoming value.
	if (OverrideFOV > 0.f)
	{
		OutView.FOV = OverrideFOV;
	}
}
