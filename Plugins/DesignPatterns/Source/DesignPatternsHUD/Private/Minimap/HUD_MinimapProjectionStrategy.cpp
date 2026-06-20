// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Minimap/HUD_MinimapProjectionStrategy.h"

void UHUD_MinimapProjectionStrategy::SetWorldRadius(float InWorldRadius)
{
	// Guard against a zero/negative radius that would divide-by-zero in projection.
	WorldRadius = FMath::Max(InWorldRadius, 1.f);
}

FHUD_ProjectedPoint UHUD_MinimapProjectionStrategy::ProjectPoint_Implementation(
	const FHUD_ProjectionContext& Context) const
{
	FHUD_ProjectedPoint Out;

	// Top-down projection: work entirely on the world XY plane relative to the view origin.
	const FVector Delta = Context.WorldLocation - Context.ViewOriginWorld;
	double PlanarX = Delta.X; // world forward/back (north)
	double PlanarY = Delta.Y; // world right/left (east)

	// Optionally rotate the plane so the view direction points up on the map. Rotating the marker by the
	// NEGATIVE view yaw expresses it in a view-aligned frame (the classic rotating minimap).
	if (bRotateWithView)
	{
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(-Context.ViewYawDegrees));
		const double CosYaw = FMath::Cos(Yaw);
		const double SinYaw = FMath::Sin(Yaw);
		const double RotX = PlanarX * CosYaw - PlanarY * SinYaw;
		const double RotY = PlanarX * SinYaw + PlanarY * CosYaw;
		PlanarX = RotX;
		PlanarY = RotY;
	}

	const double SafeRadius = FMath::Max(static_cast<double>(WorldRadius), 1.0);

	// Map world-forward (X) to map-up and world-right (Y) to map-right, then normalize by radius.
	// MapYAxisSign reconciles with the consuming widget's vertical convention.
	double NormUp = PlanarX / SafeRadius;            // +up
	double NormRight = (PlanarY / SafeRadius);       // +right
	NormUp *= MapYAxisSign;

	// Store as (right, up). Distance check is in the un-clamped normalized space.
	FVector2D Norm(static_cast<float>(NormRight), static_cast<float>(NormUp));
	const float Len = Norm.Size();

	Out.bWithinRange = (Len <= 1.f);
	if (!Out.bWithinRange && Len > KINDA_SMALL_NUMBER)
	{
		// Clamp out-of-range markers onto the unit circle (edge of the minimap) so the UI can pin them.
		Norm /= Len;
	}
	Out.NormalizedPosition = Norm;

	// Bearing: 0 = map-up, clockwise positive. atan2(right, up) gives that convention directly.
	Out.BearingDegrees = FMath::RadiansToDegrees(FMath::Atan2(Norm.X, Norm.Y));

	return Out;
}
