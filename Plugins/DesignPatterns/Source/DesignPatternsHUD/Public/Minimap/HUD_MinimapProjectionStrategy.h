// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HUD_MinimapProjectionStrategy.generated.h"

/**
 * Input to a single projection: the world point to project plus the view frame (player position + yaw)
 * the projection is relative to. Kept as a plain value struct so projection is a pure function.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_ProjectionContext
{
	GENERATED_BODY()

	/** World location of the marker being projected. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	FVector WorldLocation = FVector::ZeroVector;

	/** World location the minimap is centered on (usually the local player / camera). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	FVector ViewOriginWorld = FVector::ZeroVector;

	/** View yaw (degrees) the map should be rotated by when bRotateWithView is set. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	float ViewYawDegrees = 0.f;
};

/**
 * Result of projecting one marker onto the minimap's local 2D space.
 *
 * NormalizedPosition is in a [-1,1] x [-1,1] box centered on the map (so a view widget can multiply by
 * its half-extent regardless of pixel size). bWithinRange tells the ViewModel whether the marker fell
 * inside the configured world radius; out-of-range markers are still returned (clamped to the edge) so
 * the UI can show an edge-pinned "off-map" arrow if it wants.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_ProjectedPoint
{
	GENERATED_BODY()

	/** Normalized minimap-local position, components in [-1, 1]; (0,0) is map center. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	FVector2D NormalizedPosition = FVector2D::ZeroVector;

	/** Heading (degrees, 0 = map-up) from center to the marker — for edge arrows / off-map indicators. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	float BearingDegrees = 0.f;

	/** True if the marker is within the configured world radius (false = clamped to the edge). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	bool bWithinRange = true;
};

/**
 * Strategy object (Strategy pattern) that projects world locations onto a 2D minimap.
 *
 * The shipped concrete behaviour is a top-down (XY-plane) projection that optionally rotates with the
 * view yaw — the classic rotating-minimap. It is a swappable UObject so a game can subclass it for a
 * fixed-north map, an isometric skew, a fog-of-war clamp, etc., without touching the ViewModel. All knobs
 * are EditAnywhere tunables (no magic constants); the ViewModel owns one instanced strategy and delegates
 * every per-marker projection to ProjectPoint.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced,
	meta = (DisplayName = "HUD Minimap Projection Strategy"))
class DESIGNPATTERNSHUD_API UHUD_MinimapProjectionStrategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Project a single world point into normalized minimap space given the view frame.
	 * Pure / side-effect free so it can be called for every marker each refresh.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	FHUD_ProjectedPoint ProjectPoint(const FHUD_ProjectionContext& Context) const;
	virtual FHUD_ProjectedPoint ProjectPoint_Implementation(const FHUD_ProjectionContext& Context) const;

	/** The world radius (cm) mapped to the edge of the minimap. Tunable; never a code constant. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	float GetWorldRadius() const { return WorldRadius; }

	/** Set the world radius (e.g. when zooming the minimap). Clamped to a small positive minimum. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetWorldRadius(float InWorldRadius);

protected:
	/**
	 * World radius (cm) that maps to the minimap edge (normalized 1.0). Markers beyond this are clamped to
	 * the edge and flagged out-of-range. Default is a sensible mid-range fallback documented as a tunable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap",
		meta = (ClampMin = "1.0", UIMin = "100.0"))
	float WorldRadius = 4000.f;

	/**
	 * When true the map rotates so the view direction is always "up" (rotating minimap); when false the
	 * map is fixed-orientation (north-up) and only the player blip rotates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	bool bRotateWithView = true;

	/**
	 * Sign applied to the projected Y axis. UI space conventionally grows downward; +1 keeps world +Y as
	 * map-up, -1 flips it. Exposed so designers reconcile with their widget's coordinate convention.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap",
		meta = (ClampMin = "-1", ClampMax = "1"))
	float MapYAxisSign = 1.f;
};
