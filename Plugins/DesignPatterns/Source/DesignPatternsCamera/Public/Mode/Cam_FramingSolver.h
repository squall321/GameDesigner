// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_FramingSolver.generated.h"

/**
 * Designer-authored framing parameters for the cinematic/framing solver. Pure data: all numbers are
 * EditAnywhere with documented defensive defaults so there are no magic numbers in the math.
 *
 * Used by FCam_FramingSolver to place the camera for a single subject (rule-of-thirds offset) or two
 * subjects (lock-on dual framing: midpoint look-at + a distance derived from their separation and the
 * FOV). Dolly-zoom couples FOV and distance so the subject's screen size stays constant ("Vertigo").
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_FramingParams
{
	GENERATED_BODY()

	/**
	 * Rule-of-thirds horizontal offset as a fraction of the screen half-width [-1,1]. 0 centres the
	 * subject; ~0.33 places it on the right third line, -0.33 on the left. Applied as a lateral camera
	 * shift so the subject appears off-centre without rotating away from it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ThirdsOffset = 0.33f;

	/** Baseline camera distance (cm) from the subject when framing a single target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (ClampMin = "0.0", Units = "cm"))
	float BaseDistance = 400.f;

	/** Extra padding (cm) added around two subjects so a dual-target frame leaves headroom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (ClampMin = "0.0", Units = "cm"))
	float DualPadding = 150.f;

	/** Horizontal field of view (deg) the framing requests. Also the reference FOV for dolly-zoom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 50.f;

	/**
	 * Reference distance (cm) at which the authored FieldOfView is exact. When dolly-zoom is enabled the
	 * solver recomputes FOV from the actual distance so the subject's apparent size stays constant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (ClampMin = "1.0", Units = "cm"))
	float DollyReferenceDistance = 400.f;

	/** Vertical offset (cm) applied to the look-at point so the subject sits at a pleasing screen height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing", meta = (Units = "cm"))
	float LookAtHeightOffset = 40.f;

	/** When true the solver recomputes FOV from the actual distance (dolly-zoom / "Vertigo" effect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Framing")
	bool bDollyZoom = false;

	FCam_FramingParams() = default;

	/** True when this framing should drive a dolly-zoom FOV instead of a fixed FieldOfView. */
	bool UsesDollyZoom() const { return bDollyZoom; }
};

/**
 * Pure framing math reused by UCam_CinematicMode. Stateless static helpers — no engine camera access,
 * no member state. Each returns a fully-formed FCam_CameraView (location/rotation/FOV) the mode can
 * blend or ease toward. Kept out of the mode so the math is independently testable.
 */
struct DESIGNPATTERNSCAMERA_API FCam_FramingSolver
{
	/**
	 * Frame a single subject. Places the camera BaseDistance behind the subject along Look's forward,
	 * applies the rule-of-thirds lateral shift, and aims at the subject (raised by LookAtHeightOffset).
	 * @param Pivot   Subject world location.
	 * @param Look    Desired look direction (e.g. control rotation) the framing orients around.
	 * @param Params  Framing parameters.
	 */
	static FCam_CameraView SolveSingle(const FVector& Pivot, const FRotator& Look, const FCam_FramingParams& Params);

	/**
	 * Frame two subjects (lock-on dual framing). Aims at their midpoint and backs the camera off far
	 * enough that both fit within the FOV (separation + DualPadding), keeping Look's orientation.
	 * @param A       First subject world location (usually the player).
	 * @param B       Second subject world location (usually the locked target).
	 * @param Look    Desired look direction the framing orients around.
	 * @param Params  Framing parameters.
	 */
	static FCam_CameraView SolveDual(const FVector& A, const FVector& B, const FRotator& Look, const FCam_FramingParams& Params);

	/**
	 * Compute the dolly-zoom FOV that keeps a subject of fixed size constant when the camera is at
	 * ActualDistance instead of the reference distance. Pure trig; clamped to a sane FOV range.
	 */
	static float ComputeDollyZoomFOV(float ActualDistance, const FCam_FramingParams& Params);
};
