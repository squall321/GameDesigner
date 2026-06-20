// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Director/Cam_CameraModifier.h"
#include "Core/DPLog.h"

UCam_CameraModifier::UCam_CameraModifier()
{
	// The director drives enable/disable explicitly; default to enabled so it applies once registered.
	// Priority left at the engine default — the director's view is the authoritative framing, and any
	// shake modifiers run after this and layer additively, which is the desired ordering.
}

bool UCam_CameraModifier::ModifyCamera(float DeltaTime, FVector ViewLocation, FRotator ViewRotation, float FOV,
	FVector& NewViewLocation, FRotator& NewViewRotation, float& NewFOV)
{
	// Default pass-through: keep the manager's POV.
	NewViewLocation = ViewLocation;
	NewViewRotation = ViewRotation;
	NewFOV = FOV;

	if (!bHasDesiredView)
	{
		return false; // nothing to apply; let later modifiers run.
	}

	// Alpha is the modifier's own enable/disable fade (0..1). Blend the manager POV toward our desired
	// view by Alpha so toggling the director on/off eases rather than snaps.
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	NewViewLocation = FMath::Lerp(ViewLocation, DesiredView.Location, A);
	NewViewRotation = FQuat::Slerp(ViewRotation.Quaternion(), DesiredView.Rotation.Quaternion(), A).Rotator();
	NewFOV = FMath::Lerp(FOV, DesiredView.FOV, A);

	// Return false: we overwrote the POV but want shakes / post modifiers further down the chain to run.
	return false;
}

void UCam_CameraModifier::SetDesiredView(const FCam_CameraView& InView)
{
	DesiredView = InView;
	bHasDesiredView = true;
}

void UCam_CameraModifier::ClearDesiredView()
{
	bHasDesiredView = false;
}
