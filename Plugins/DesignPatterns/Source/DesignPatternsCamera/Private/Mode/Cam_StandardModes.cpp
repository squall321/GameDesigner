// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_StandardModes.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

// ---------------------------------------------------------------------------------------------
// Third-Person Follow
// ---------------------------------------------------------------------------------------------

UCam_ThirdPersonFollowMode::UCam_ThirdPersonFollowMode()
{
}

void UCam_ThirdPersonFollowMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	// Seed smoothing state from the live camera so the first frame eases from the current view.
	SmoothedPivot = Context.PivotLocation;
	SmoothedRotation = Context.PreviousCameraRotation.IsNearlyZero() ? Context.ControlRotation : Context.PreviousCameraRotation;
	bSeeded = true;
}

void UCam_ThirdPersonFollowMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	// Clamp pitch within designer limits.
	FRotator DesiredRot = Context.ControlRotation;
	DesiredRot.Pitch = FMath::ClampAngle(DesiredRot.Pitch, MinPitch, MaxPitch);
	DesiredRot.Roll = 0.f;

	if (!bSeeded)
	{
		SmoothedPivot = Context.PivotLocation;
		SmoothedRotation = DesiredRot;
		bSeeded = true;
	}

	// Smooth the pivot and boom rotation independently for a spring-arm feel.
	SmoothedPivot = SmoothVector(SmoothedPivot, Context.PivotLocation, LocationLag, DeltaTime);
	SmoothedRotation = SmoothRotator(SmoothedRotation, DesiredRot, RotationLag, DeltaTime);

	// Build the look frame from the smoothed rotation and apply the socket offset in that frame.
	const FRotationMatrix RotMatrix(SmoothedRotation);
	const FVector Forward = RotMatrix.GetUnitAxis(EAxis::X);
	const FVector Right = RotMatrix.GetUnitAxis(EAxis::Y);
	const FVector Up = RotMatrix.GetUnitAxis(EAxis::Z);

	const FVector OffsetPivot = SmoothedPivot + TargetOffset
		+ Forward * SocketOffset.X
		+ Right * SocketOffset.Y
		+ Up * SocketOffset.Z;

	// Boom backward from the offset pivot along the look direction.
	OutView.Location = OffsetPivot - Forward * ArmLength;
	OutView.Rotation = SmoothedRotation;
	OutView.FOV = FieldOfView;
}

// ---------------------------------------------------------------------------------------------
// First-Person
// ---------------------------------------------------------------------------------------------

UCam_FirstPersonMode::UCam_FirstPersonMode()
{
	// First-person blends should be near-instant by default; override base ease times.
	BlendInTime = 0.15f;
	BlendOutTime = 0.15f;
}

void UCam_FirstPersonMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	FRotator DesiredRot = Context.ControlRotation;
	DesiredRot.Pitch = FMath::ClampAngle(DesiredRot.Pitch, MinPitch, MaxPitch);
	DesiredRot.Roll = 0.f;

	if (!bSeeded)
	{
		SmoothedRotation = DesiredRot;
		bSeeded = true;
	}
	SmoothedRotation = SmoothRotator(SmoothedRotation, DesiredRot, RotationLag, DeltaTime);

	// Eye position: pivot plus eye offset expressed in the view target's local frame if available,
	// otherwise in the smoothed look frame.
	FVector EyeWorld = Context.PivotLocation;
	if (const AActor* Target = Context.ViewTarget.Get())
	{
		EyeWorld += Target->GetActorTransform().TransformVector(EyeOffset);
	}
	else
	{
		const FRotationMatrix RotMatrix(SmoothedRotation);
		EyeWorld += RotMatrix.GetUnitAxis(EAxis::X) * EyeOffset.X
			+ RotMatrix.GetUnitAxis(EAxis::Y) * EyeOffset.Y
			+ RotMatrix.GetUnitAxis(EAxis::Z) * EyeOffset.Z;
	}

	OutView.Location = EyeWorld;
	OutView.Rotation = SmoothedRotation;
	OutView.FOV = FieldOfView;
}

// ---------------------------------------------------------------------------------------------
// Top-Down
// ---------------------------------------------------------------------------------------------

UCam_TopDownMode::UCam_TopDownMode()
{
}

void UCam_TopDownMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	SmoothedPivot = Context.PivotLocation;
	bSeeded = true;
}

void UCam_TopDownMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	if (!bSeeded)
	{
		SmoothedPivot = Context.PivotLocation;
		bSeeded = true;
	}
	SmoothedPivot = SmoothVector(SmoothedPivot, Context.PivotLocation, LocationLag, DeltaTime);

	const float Yaw = bFollowControlYaw ? Context.ControlRotation.Yaw : FixedYaw;
	const FRotator FramingRot(0.f, Yaw, 0.f);
	const FVector BackDir = FramingRot.Vector(); // unit vector along framing yaw on the horizontal plane

	// Camera sits up by Height and behind the pivot by HorizontalDistance along the framing yaw.
	OutView.Location = SmoothedPivot + Context.WorldUp * Height - BackDir * HorizontalDistance;
	OutView.Rotation = FRotator(Pitch, Yaw, 0.f);
	OutView.FOV = FieldOfView;
}

// ---------------------------------------------------------------------------------------------
// Orbit
// ---------------------------------------------------------------------------------------------

UCam_OrbitMode::UCam_OrbitMode()
{
}

void UCam_OrbitMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	SmoothedRotation = Context.ControlRotation;
	AutoOrbitYaw = 0.f;
	bSeeded = true;
}

void UCam_OrbitMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	// Advance auto-orbit yaw independently so it works even without look input.
	AutoOrbitYaw += AutoOrbitRate * DeltaTime;

	FRotator DesiredRot = Context.ControlRotation;
	DesiredRot.Yaw += AutoOrbitYaw;
	DesiredRot.Pitch = FMath::ClampAngle(DesiredRot.Pitch, MinPitch, MaxPitch);
	DesiredRot.Roll = 0.f;

	if (!bSeeded)
	{
		SmoothedRotation = DesiredRot;
		bSeeded = true;
	}
	SmoothedRotation = SmoothRotator(SmoothedRotation, DesiredRot, RotationLag, DeltaTime);

	const float ClampedDistance = FMath::Clamp(Distance, MinDistance, MaxDistance);
	const FVector Forward = SmoothedRotation.Vector();

	OutView.Location = Context.PivotLocation - Forward * ClampedDistance;
	OutView.Rotation = SmoothedRotation;
	OutView.FOV = FieldOfView;
}

// ---------------------------------------------------------------------------------------------
// Fixed / Scripted
// ---------------------------------------------------------------------------------------------

UCam_FixedMode::UCam_FixedMode()
{
	// Cinematic-style longer eases by default for scripted cameras.
	BlendInTime = 0.6f;
	BlendOutTime = 0.6f;
}

void UCam_FixedMode::UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	OutView.Location = WorldLocation;
	OutView.FOV = FieldOfView;

	if (bLookAtPivot)
	{
		const FVector LookTarget = Context.PivotLocation + LookAtOffset;
		const FVector Dir = (LookTarget - WorldLocation);
		OutView.Rotation = Dir.IsNearlyZero() ? WorldRotation : Dir.Rotation();
	}
	else
	{
		OutView.Rotation = WorldRotation;
	}
}
