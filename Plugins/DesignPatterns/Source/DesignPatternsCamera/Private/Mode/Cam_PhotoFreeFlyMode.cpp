// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_PhotoFreeFlyMode.h"

UCam_PhotoFreeFlyMode::UCam_PhotoFreeFlyMode()
{
	// Photo mode should not auto-blend on the way in — the component drives the transition; keep a
	// short ease so the entry is not a hard cut.
	BlendInTime = 0.25f;
	BlendOutTime = 0.25f;
}

void UCam_PhotoFreeFlyMode::OnEnterStack_Implementation(const FCam_ViewContext& Context)
{
	Super::OnEnterStack_Implementation(Context);

	// Seed the free transform from where the camera actually is, so entering photo mode is seamless.
	FreeLocation = Context.PreviousCameraLocation;
	FreeRotation = Context.PreviousCameraRotation;
	FreeRotation.Roll = 0.f;
	FreeRoll = Context.PreviousCameraRotation.Roll;
	FreeFOV = FMath::Clamp(FreeFOV, MinFOV, MaxFOV);
	PivotAnchor = Context.PivotLocation;
	bSeeded = true;
}

void UCam_PhotoFreeFlyMode::ApplyInput(const FCam_PhotoInput& In)
{
	if (!bSeeded)
	{
		// Defensive: if input arrives before OnEnterStack seeded us, anchor at the free location.
		PivotAnchor = FreeLocation;
		bSeeded = true;
	}

	// Integrate look first so movement is relative to the new orientation.
	FreeRotation.Yaw += In.LookDelta.Yaw;
	FreeRotation.Pitch = FMath::Clamp(FreeRotation.Pitch + In.LookDelta.Pitch, MinPitch, MaxPitch);
	FreeRotation.Roll = 0.f;

	FreeRoll += In.RollDelta * RollSpeed;

	// Movement is in the camera's local frame (X fwd / Y right / Z up) at MoveSpeed.
	const FRotationMatrix RotMatrix(FreeRotation);
	const FVector WorldMove =
		RotMatrix.GetScaledAxis(EAxis::X) * In.MoveDelta.X +
		RotMatrix.GetScaledAxis(EAxis::Y) * In.MoveDelta.Y +
		FVector::UpVector * In.MoveDelta.Z;
	FreeLocation += WorldMove * MoveSpeed;

	// Clamp travel to a sphere around the entry pivot so the photo camera can't escape the scene.
	if (MaxTravelRadius > 0.f)
	{
		const FVector Offset = FreeLocation - PivotAnchor;
		const float Dist = Offset.Size();
		if (Dist > MaxTravelRadius)
		{
			FreeLocation = PivotAnchor + Offset * (MaxTravelRadius / Dist);
		}
	}

	FreeFOV = FMath::Clamp(FreeFOV + In.FOVDelta, MinFOV, MaxFOV);
}

void UCam_PhotoFreeFlyMode::UpdateView_Implementation(float /*DeltaTime*/, const FCam_ViewContext& Context, FCam_CameraView& OutView)
{
	if (!bSeeded)
	{
		// Not yet entered (e.g. evaluated before OnEnterStack): report the live camera unchanged.
		OutView.Location = Context.PreviousCameraLocation;
		OutView.Rotation = Context.PreviousCameraRotation;
		OutView.FOV = FreeFOV;
		return;
	}

	FRotator FinalRot = FreeRotation;
	FinalRot.Roll = FreeRoll; // apply accumulated roll on output (kept separate from look-pitch clamp).

	OutView.Location = FreeLocation;
	OutView.Rotation = FinalRot;
	OutView.FOV = FreeFOV;
}
