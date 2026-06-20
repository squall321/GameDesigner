// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_StandardModes.generated.h"

/**
 * Classic over-the-shoulder follow camera. Orbits the pivot at a fixed boom (arm) length using the
 * player's control rotation, with positional/rotational lag and a shoulder offset. Wraps the same
 * math a USpringArmComponent does, but as a composable, blendable stack mode so it can cross-fade
 * with other modes (e.g. blend to first-person on aim).
 *
 * NOTE: this mode computes a desired POV only. Collision/probe handling, if desired, is layered by
 * the director or an engine spring arm on the pawn — kept out here so the mode stays pure data->view.
 */
UCLASS(meta = (DisplayName = "Third-Person Follow"))
class DESIGNPATTERNSCAMERA_API UCam_ThirdPersonFollowMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_ThirdPersonFollowMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;

protected:
	/** Distance from pivot to camera along the look direction (boom length). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow", meta = (ClampMin = "0.0", Units = "cm"))
	float ArmLength = 350.f;

	/** Offset applied to the pivot in the look frame (X fwd, Y right/shoulder, Z up) before booming out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow")
	FVector SocketOffset = FVector(0.f, 60.f, 60.f);

	/** Additional offset applied in world/pivot space (e.g. raise the framing target). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow")
	FVector TargetOffset = FVector(0.f, 0.f, 0.f);

	/** Positional lag time-constant (s). 0 = rigid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float LocationLag = 0.08f;

	/** Rotational lag time-constant (s). 0 = rigid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float RotationLag = 0.05f;

	/** Minimum pitch (deg) the camera may look at; clamps control pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow|Pitch", meta = (Units = "deg"))
	float MinPitch = -70.f;

	/** Maximum pitch (deg) the camera may look at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow|Pitch", meta = (Units = "deg"))
	float MaxPitch = 70.f;

	/** Field of view (deg) this mode requests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Follow", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 90.f;

private:
	/** Smoothed pivot, carried across frames for lag. */
	UPROPERTY(Transient)
	FVector SmoothedPivot = FVector::ZeroVector;

	/** Smoothed boom rotation, carried across frames for lag. */
	UPROPERTY(Transient)
	FRotator SmoothedRotation = FRotator::ZeroRotator;

	/** Whether the smoothed state has been seeded (first valid frame). */
	UPROPERTY(Transient)
	bool bSeeded = false;
};

/**
 * First-person camera: sits at an eye offset from the pivot and looks directly along control rotation.
 * No arm, no lag by default (lag is jarring in first person) but exposes optional micro-lag.
 */
UCLASS(meta = (DisplayName = "First-Person"))
class DESIGNPATTERNSCAMERA_API UCam_FirstPersonMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_FirstPersonMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;

protected:
	/** Eye offset from the pivot, in the view target's local frame (X fwd, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirstPerson")
	FVector EyeOffset = FVector(0.f, 0.f, 0.f);

	/** Optional rotational micro-lag (s) for weighty feel. 0 = perfectly rigid to look input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirstPerson|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float RotationLag = 0.f;

	/** Minimum pitch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirstPerson|Pitch", meta = (Units = "deg"))
	float MinPitch = -85.f;

	/** Maximum pitch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirstPerson|Pitch", meta = (Units = "deg"))
	float MaxPitch = 85.f;

	/** Field of view (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FirstPerson", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 100.f;

private:
	UPROPERTY(Transient)
	FRotator SmoothedRotation = FRotator::ZeroRotator;

	UPROPERTY(Transient)
	bool bSeeded = false;
};

/**
 * Top-down / isometric camera: places the camera at a fixed height and pitch above the pivot, looking
 * down. Yaw can either follow control rotation or stay fixed (strategy/ARPG framing).
 */
UCLASS(meta = (DisplayName = "Top-Down"))
class DESIGNPATTERNSCAMERA_API UCam_TopDownMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_TopDownMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;

protected:
	/** Camera height above the pivot (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown", meta = (ClampMin = "0.0", Units = "cm"))
	float Height = 900.f;

	/** Horizontal back-off distance from the pivot along the framing yaw (cm); 0 = straight overhead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown", meta = (ClampMin = "0.0", Units = "cm"))
	float HorizontalDistance = 600.f;

	/** Downward pitch (deg, negative looks down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown", meta = (Units = "deg"))
	float Pitch = -55.f;

	/** If true the framing yaw follows control rotation; if false it stays at FixedYaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown")
	bool bFollowControlYaw = false;

	/** Fixed yaw (deg) used when bFollowControlYaw is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown", meta = (EditCondition = "!bFollowControlYaw", Units = "deg"))
	float FixedYaw = 0.f;

	/** Positional lag time-constant (s) so the camera trails the pivot smoothly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float LocationLag = 0.15f;

	/** Field of view (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TopDown", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 80.f;

private:
	UPROPERTY(Transient)
	FVector SmoothedPivot = FVector::ZeroVector;

	UPROPERTY(Transient)
	bool bSeeded = false;
};

/**
 * Orbit / inspection camera: orbits the pivot at a controllable distance using control rotation for
 * yaw/pitch, with distance clamps and optional auto-orbit. Good for character menus, photo mode,
 * vehicle showcases. Requests the look-capture input mode while active (handled by the director).
 */
UCLASS(meta = (DisplayName = "Orbit"))
class DESIGNPATTERNSCAMERA_API UCam_OrbitMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_OrbitMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;

	/** True if this mode wants the director to push the look-capture input mode while active. */
	bool WantsLookCaptureInput() const { return bCaptureLookInput; }

protected:
	/** Orbit radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0.0", Units = "cm"))
	float Distance = 300.f;

	/** Minimum allowed orbit radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0.0", Units = "cm"))
	float MinDistance = 120.f;

	/** Maximum allowed orbit radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 800.f;

	/** Minimum pitch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Pitch", meta = (Units = "deg"))
	float MinPitch = -80.f;

	/** Maximum pitch (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Pitch", meta = (Units = "deg"))
	float MaxPitch = 80.f;

	/** Auto-orbit yaw rate (deg/s) applied when there is no look input (0 = no auto-orbit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (Units = "deg/s"))
	float AutoOrbitRate = 0.f;

	/** Rotational lag (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float RotationLag = 0.06f;

	/** If true, the director pushes the look-capture input mode while this mode is the top mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bCaptureLookInput = true;

	/** Field of view (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 70.f;

private:
	UPROPERTY(Transient)
	FRotator SmoothedRotation = FRotator::ZeroRotator;

	/** Accumulated auto-orbit yaw (deg) so it advances even when control rotation is static. */
	UPROPERTY(Transient)
	float AutoOrbitYaw = 0.f;

	UPROPERTY(Transient)
	bool bSeeded = false;
};

/**
 * Fixed/scripted camera: ignores the view target entirely and reports a designer-authored world POV.
 * Used for security-cam framing, cutscene establishing shots, fixed-angle rooms. Optionally looks at
 * the pivot so the subject stays framed while the camera position stays put.
 */
UCLASS(meta = (DisplayName = "Fixed"))
class DESIGNPATTERNSCAMERA_API UCam_FixedMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_FixedMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;

protected:
	/** Fixed world location of the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed")
	FVector WorldLocation = FVector::ZeroVector;

	/** Fixed world rotation used when bLookAtPivot is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed", meta = (EditCondition = "!bLookAtPivot"))
	FRotator WorldRotation = FRotator::ZeroRotator;

	/** If true the camera aims at the (offset) pivot so the subject stays framed as it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed")
	bool bLookAtPivot = true;

	/** Offset added to the pivot when computing the look-at target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed", meta = (EditCondition = "bLookAtPivot"))
	FVector LookAtOffset = FVector(0.f, 0.f, 40.f);

	/** Field of view (deg). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fixed", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 60.f;
};
