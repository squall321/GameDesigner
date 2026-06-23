// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DPLayoutTypes.generated.h"

/**
 * Coarse viewport-size class the responsive layout system classifies into.
 *
 * Breakpoints are resolved from data-driven width thresholds (UDP_ResponsiveLayoutDataAsset), so
 * the names are advisory and the actual pixel boundaries are entirely designer-authored. Widgets
 * react to the breakpoint (swap a layout variant, change column counts) without hardcoding sizes.
 */
UENUM(BlueprintType)
enum class EDP_UIBreakpoint : uint8
{
	/** Smallest class (e.g. handheld / small phone). */
	Compact,
	/** Medium class (e.g. tablet / small window). */
	Medium,
	/** Large class (e.g. desktop / console-at-distance). */
	Expanded,
	/** Largest class (e.g. ultra-wide / 4K). */
	Wide
};

/** Fired when the resolved breakpoint, safe margin, or effective DPI changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDP_OnLayoutChanged, EDP_UIBreakpoint, NewBreakpoint);

/**
 * Snapshot of the responsive layout state pushed to consumers / queried by widgets. Plain value
 * type so it is cheap to copy into a Blueprint or a bus payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSUI_API FDP_LayoutState
{
	GENERATED_BODY()

	/** The resolved size class. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	EDP_UIBreakpoint Breakpoint = EDP_UIBreakpoint::Expanded;

	/** Title-safe insets as an FMargin (Left, Top, Right, Bottom in px). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	FMargin SafeZoneMargin = FMargin(0);

	/** The effective DPI scale (platform DPI * accessibility UI scale). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	float EffectiveDPIScale = 1.0f;

	/** The current viewport resolution in px (best-effort; zero when unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	FIntPoint Resolution = FIntPoint::ZeroValue;
};
