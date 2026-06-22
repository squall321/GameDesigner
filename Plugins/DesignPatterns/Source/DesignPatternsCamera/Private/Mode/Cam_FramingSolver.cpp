// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mode/Cam_FramingSolver.h"

FCam_CameraView FCam_FramingSolver::SolveSingle(const FVector& Pivot, const FRotator& Look, const FCam_FramingParams& Params)
{
	FCam_CameraView View;

	const FVector Forward = Look.Vector();
	const FVector Right = FRotationMatrix(Look).GetScaledAxis(EAxis::Y);

	// Look-at point raised slightly so the subject sits a touch above centre.
	const FVector LookAt = Pivot + FVector(0.f, 0.f, Params.LookAtHeightOffset);

	// Back the camera off along the look direction.
	const float Distance = FMath::Max(Params.BaseDistance, 1.f);
	FVector CamLocation = LookAt - Forward * Distance;

	// Rule-of-thirds lateral shift: move the camera sideways so the subject sits off the centre line.
	// The shift magnitude scales with distance and the authored thirds fraction.
	const float LateralShift = Params.ThirdsOffset * Distance * FMath::Tan(FMath::DegreesToRadians(Params.FieldOfView * 0.5f));
	CamLocation += Right * LateralShift;

	View.Location = CamLocation;
	View.Rotation = (LookAt - CamLocation).Rotation();
	View.FOV = Params.UsesDollyZoom() ? ComputeDollyZoomFOV(Distance, Params) : Params.FieldOfView;
	return View;
}

FCam_CameraView FCam_FramingSolver::SolveDual(const FVector& A, const FVector& B, const FRotator& Look, const FCam_FramingParams& Params)
{
	FCam_CameraView View;

	const FVector Midpoint = (A + B) * 0.5f + FVector(0.f, 0.f, Params.LookAtHeightOffset);
	const float Separation = FVector::Dist(A, B);

	// Distance needed for both subjects (separation + padding) to fit within the horizontal FOV:
	//   half-extent = separation/2 + padding ; distance = half-extent / tan(fov/2).
	const float HalfExtent = Separation * 0.5f + Params.DualPadding;
	const float HalfFovRad = FMath::DegreesToRadians(FMath::Clamp(Params.FieldOfView, 5.f, 170.f) * 0.5f);
	const float TanHalf = FMath::Max(FMath::Tan(HalfFovRad), KINDA_SMALL_NUMBER);
	const float FitDistance = FMath::Max(HalfExtent / TanHalf, Params.BaseDistance);

	const FVector Forward = Look.Vector();
	const FVector CamLocation = Midpoint - Forward * FitDistance;

	View.Location = CamLocation;
	View.Rotation = (Midpoint - CamLocation).Rotation();
	View.FOV = Params.UsesDollyZoom() ? ComputeDollyZoomFOV(FitDistance, Params) : Params.FieldOfView;
	return View;
}

float FCam_FramingSolver::ComputeDollyZoomFOV(float ActualDistance, const FCam_FramingParams& Params)
{
	// Keep apparent subject size constant: the on-screen extent at the reference distance/FOV is
	//   E = 2 * RefDist * tan(RefFov/2). To preserve E at ActualDistance:
	//   tan(NewFov/2) = (RefDist / ActualDist) * tan(RefFov/2).
	const float RefDist = FMath::Max(Params.DollyReferenceDistance, 1.f);
	const float ActDist = FMath::Max(ActualDistance, 1.f);
	const float RefHalfFovRad = FMath::DegreesToRadians(FMath::Clamp(Params.FieldOfView, 5.f, 170.f) * 0.5f);
	const float NewTan = (RefDist / ActDist) * FMath::Tan(RefHalfFovRad);
	const float NewHalfFovDeg = FMath::RadiansToDegrees(FMath::Atan(NewTan));
	return FMath::Clamp(NewHalfFovDeg * 2.f, 5.f, 170.f);
}
