// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Display/UPlat_DisplayLibrary.h"
#include "Display/UPlat_DisplaySubsystem.h"
#include "Core/DPSubsystemLibrary.h"

FMargin UPlat_DisplayLibrary::InsetsToMargin(const FVector4& Insets)
{
	// FVector4 carries (Left, Top, Right, Bottom); FMargin's ctor is (Left, Top, Right, Bottom).
	return FMargin(Insets.X, Insets.Y, Insets.Z, Insets.W);
}

FMargin UPlat_DisplayLibrary::InsetMargin(const FMargin& In, const FMargin& Safe)
{
	return FMargin(
		In.Left + Safe.Left,
		In.Top + Safe.Top,
		In.Right + Safe.Right,
		In.Bottom + Safe.Bottom);
}

FVector2D UPlat_DisplayLibrary::ApplyTitleSafe(FVector2D ScreenPos, const FPlat_DisplayMetrics& M)
{
	const float MinX = M.TitleSafeInsetsPx.X;
	const float MinY = M.TitleSafeInsetsPx.Y;
	const float MaxX = FMath::Max(MinX, (float)M.ResolutionPx.X - M.TitleSafeInsetsPx.Z);
	const float MaxY = FMath::Max(MinY, (float)M.ResolutionPx.Y - M.TitleSafeInsetsPx.W);

	return FVector2D(
		FMath::Clamp(ScreenPos.X, MinX, MaxX),
		FMath::Clamp(ScreenPos.Y, MinY, MaxY));
}

FPlat_DisplayMetrics UPlat_DisplayLibrary::GetCurrentMetrics(const UObject* WorldContextObject)
{
	if (UPlat_DisplaySubsystem* Display = FDP_SubsystemStatics::GetGameInstanceSubsystem<UPlat_DisplaySubsystem>(WorldContextObject))
	{
		return Display->GetDisplayMetrics();
	}
	return FPlat_DisplayMetrics();
}
