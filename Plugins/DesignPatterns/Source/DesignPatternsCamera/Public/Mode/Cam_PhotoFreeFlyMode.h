// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_PhotoFreeFlyMode.generated.h"

/**
 * Accumulated photo-mode input for one frame, produced by UCam_PhotoModeComponent from the local
 * player's input and pushed into the live UCam_PhotoFreeFlyMode each tick. Pure value type, never
 * replicated — photo mode is entirely local/cosmetic.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_PhotoInput
{
	GENERATED_BODY()

	/** Local-space move delta this frame (X fwd, Y right, Z up), pre-speed. Consumed (zeroed) on apply. */
	UPROPERTY(BlueprintReadWrite, Category = "Camera|Photo")
	FVector MoveDelta = FVector::ZeroVector;

	/** Look delta this frame (pitch/yaw in deg). Consumed on apply. */
	UPROPERTY(BlueprintReadWrite, Category = "Camera|Photo")
	FRotator LookDelta = FRotator::ZeroRotator;

	/** Roll delta this frame (deg), pre-speed. Consumed on apply. */
	UPROPERTY(BlueprintReadWrite, Category = "Camera|Photo")
	float RollDelta = 0.f;

	/** FOV delta this frame (deg, negative zooms in). Consumed on apply. */
	UPROPERTY(BlueprintReadWrite, Category = "Camera|Photo")
	float FOVDelta = 0.f;

	FCam_PhotoInput() = default;

	/** Reset all accumulated deltas to zero (after they have been applied). */
	void Reset() { *this = FCam_PhotoInput(); }
};

/**
 * Free-fly photo-mode camera. Integrates accumulated FCam_PhotoInput deltas into a free location /
 * rotation / roll / FOV that the camera reports each frame, with the free location clamped to a sphere
 * of MaxTravelRadius around the pivot so the player cannot fly the photo camera off the level.
 *
 * The owning UCam_PhotoModeComponent feeds input by reaching this live instance through the director's
 * public GetStack()->GetTopMode() + Cast (the mode is guaranteed top while photo mode is active at the
 * photo priority) — no new stack API is required. Pure data -> view; cosmetic; never replicated.
 */
UCLASS(meta = (DisplayName = "Photo Free-Fly"))
class DESIGNPATTERNSCAMERA_API UCam_PhotoFreeFlyMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_PhotoFreeFlyMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;

	/**
	 * Apply accumulated input deltas into the free transform/FOV. Called by the photo component each
	 * tick BEFORE the stack evaluates. Integrates against the cached pivot for the travel clamp.
	 */
	void ApplyInput(const FCam_PhotoInput& In);

protected:
	/** Movement speed (cm/s) multiplied into MoveDelta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MoveSpeed = 400.f;

	/** Roll speed (deg/s) multiplied into RollDelta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo", meta = (ClampMin = "0.0", Units = "deg/s"))
	float RollSpeed = 60.f;

	/** Minimum FOV (deg) the photo camera may zoom to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float MinFOV = 20.f;

	/** Maximum FOV (deg) the photo camera may zoom to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float MaxFOV = 120.f;

	/** Maximum distance (cm) the free camera may travel from the pivot it entered at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxTravelRadius = 1500.f;

	/** Minimum pitch (deg) the free look is clamped to (prevents flipping over). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Pitch", meta = (Units = "deg"))
	float MinPitch = -89.f;

	/** Maximum pitch (deg) the free look is clamped to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Pitch", meta = (Units = "deg"))
	float MaxPitch = 89.f;

private:
	/** Free camera world location, integrated from move input, clamped to MaxTravelRadius of PivotAnchor. */
	UPROPERTY(Transient)
	FVector FreeLocation = FVector::ZeroVector;

	/** Free camera world rotation (pitch/yaw), integrated from look input. */
	UPROPERTY(Transient)
	FRotator FreeRotation = FRotator::ZeroRotator;

	/** Free roll (deg) integrated from roll input, applied as the rotation's roll. */
	UPROPERTY(Transient)
	float FreeRoll = 0.f;

	/** Free FOV (deg) integrated from FOV input, clamped to [MinFOV, MaxFOV]. */
	UPROPERTY(Transient)
	float FreeFOV = 70.f;

	/** The pivot the photo camera entered at; the travel clamp is measured from here. */
	UPROPERTY(Transient)
	FVector PivotAnchor = FVector::ZeroVector;

	/** Whether the free transform has been seeded from the entry context. */
	UPROPERTY(Transient)
	bool bSeeded = false;
};
