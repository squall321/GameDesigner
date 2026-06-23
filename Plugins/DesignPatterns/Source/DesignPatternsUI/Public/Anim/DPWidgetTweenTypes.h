// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DPWidgetTweenTypes.generated.h"

class UCurveFloat;

/** The cosmetic property a tween animates on its target widget. */
UENUM(BlueprintType)
enum class EDP_WidgetTweenChannel : uint8
{
	/** Render opacity (0..1). */
	Opacity,
	/** Render-transform translation (px, in the widget's local space). */
	Translation,
	/** Render-transform scale (relative to the widget's render pivot). */
	Scale,
	/** Render-transform angle (degrees). */
	Angle,
	/** Color/opacity tint (UUserWidget color-and-opacity, when the target is a UUserWidget). */
	ColorAndOpacity
};

/**
 * One step in a sequenceable widget tween. A step animates a single channel from a start to an
 * end value over a duration, with an optional easing curve and an optional start delay so steps
 * can be staggered. Values are packed into FVector4 so a single struct describes every channel:
 *  - Opacity:        From/To.X used.
 *  - Translation:    From/To.X,Y used (px).
 *  - Scale:          From/To.X,Y used.
 *  - Angle:          From/To.X used (degrees).
 *  - ColorAndOpacity: From/To = RGBA.
 *
 * No magic numbers: durations/values are author-supplied (data asset / Blueprint), and the easing
 * is a designer-authored UCurveFloat (linear fallback when null).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSUI_API FDP_WidgetTweenStep
{
	GENERATED_BODY()

	/** Which cosmetic channel this step drives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween")
	EDP_WidgetTweenChannel Channel = EDP_WidgetTweenChannel::Opacity;

	/** Start value (packed per Channel; see struct doc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween")
	FVector4 From = FVector4(0, 0, 0, 0);

	/** End value (packed per Channel; see struct doc). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween")
	FVector4 To = FVector4(1, 1, 1, 1);

	/** Duration in seconds. Clamped to a tiny positive epsilon at runtime so we never divide by zero. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween", meta = (ClampMin = "0.0"))
	float DurationSeconds = 0.25f;

	/** Delay before this step starts, measured from the start of the sequence step before it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween", meta = (ClampMin = "0.0"))
	float StartDelaySeconds = 0.0f;

	/** Easing curve sampled by normalized progress (0..1 in, 0..1 out). Null => linear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tween")
	TObjectPtr<UCurveFloat> EaseCurve = nullptr;
};

/** Fired once when an anim driver finishes its current play (naturally or via Stop). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDP_OnWidgetTweenFinished, bool, bCompletedFully);
