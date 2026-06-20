// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_CameraMode.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UCam_CameraMode::UCam_CameraMode()
{
}

void UCam_CameraMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	// Base behaviour is an identity/"do nothing useful" framing: sit at the pivot looking along the
	// control rotation. Concrete modes override this. Provided so a misconfigured stack still yields
	// a finite, non-NaN view instead of an uninitialized one.
	OutView.Location = Context.PivotLocation;
	OutView.Rotation = Context.ControlRotation;
	OutView.FOV = 90.f;
}

void UCam_CameraMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	// Default: nothing transient to seed. Subclasses that smooth toward a target snapshot the
	// current camera so they ease from the live position rather than popping.
}

void UCam_CameraMode::OnExitStack_Implementation()
{
	// Default: no teardown.
}

FVector UCam_CameraMode::SmoothVector(const FVector& Current, const FVector& Target, float Lag, float DeltaTime)
{
	if (Lag <= 0.f || DeltaTime <= 0.f)
	{
		return Target;
	}
	// Frame-rate-independent exponential smoothing: alpha = 1 - exp(-dt/lag). Lag is the approximate
	// time-constant (seconds) to close ~63% of the gap.
	const float Alpha = 1.f - FMath::Exp(-DeltaTime / Lag);
	return FMath::Lerp(Current, Target, FMath::Clamp(Alpha, 0.f, 1.f));
}

FRotator UCam_CameraMode::SmoothRotator(const FRotator& Current, const FRotator& Target, float Lag, float DeltaTime)
{
	if (Lag <= 0.f || DeltaTime <= 0.f)
	{
		return Target;
	}
	const float Alpha = 1.f - FMath::Exp(-DeltaTime / Lag);
	return FQuat::Slerp(Current.Quaternion(), Target.Quaternion(), FMath::Clamp(Alpha, 0.f, 1.f)).Rotator();
}
