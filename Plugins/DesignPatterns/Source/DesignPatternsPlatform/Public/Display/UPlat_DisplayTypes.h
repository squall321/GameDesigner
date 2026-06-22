// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UPlat_DisplayTypes.generated.h"

/**
 * A resolved snapshot of the current display, used by UI to compute insets/scale and by the camera
 * photo-mode to draw framing guides. Plain BlueprintReadOnly UStruct so it can be passed to widgets,
 * logged or cached. All insets are in PIXELS as FVector4(Left, Top, Right, Bottom) so the value never
 * needs Slate to be meaningful (the FMargin conversion lives in UPlat_DisplayLibrary).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_DisplayMetrics
{
	GENERATED_BODY()

	/** Current viewport/back-buffer resolution in pixels. Defensive 1080p default before first resolve. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	FIntPoint ResolutionPx = FIntPoint(1920, 1080);

	/** Current UI DPI scale factor (1.0 = unscaled). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	float DPIScale = 1.f;

	/** Width / height of the resolution (defensive 16:9 default). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	float AspectRatio = 1.777f;

	/**
	 * Title-safe insets in pixels (Left, Top, Right, Bottom): a uniform margin pulled in from the screen
	 * edges so HUD content stays inside the TV-safe area on platforms that overscan.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	FVector4 TitleSafeInsetsPx = FVector4(0, 0, 0, 0);

	/**
	 * Display-cutout / notch insets in pixels (Left, Top, Right, Bottom): the area obscured by a
	 * hardware notch/punch-hole/rounded corner on mobile and some handhelds. Zero on most platforms.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	FVector4 DisplayCutoutInsetsPx = FVector4(0, 0, 0, 0);

	/** True when the display is taller than it is wide (portrait mobile). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Display")
	bool bIsPortrait = false;
};
