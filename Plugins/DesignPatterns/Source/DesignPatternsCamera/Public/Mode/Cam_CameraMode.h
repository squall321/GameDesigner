// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "Cam_CameraMode.generated.h"

class AActor;

/**
 * Read-only per-tick input handed to every camera mode's UpdateView. Pure data — modes never
 * mutate it. Sourced by the director from the locally-controlled view target each tick.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ViewContext
{
	GENERATED_BODY()

	/** The actor the camera is framing (usually the locally-controlled pawn). Weak — never owns it. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	TWeakObjectPtr<AActor> ViewTarget;

	/**
	 * The pivot the camera orbits / frames around in world space. Typically the view target's
	 * location plus a socket/eye offset, resolved by the director before evaluation.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	FVector PivotLocation = FVector::ZeroVector;

	/** World-space "up" used for clamping/lag math; lets games support non-Z-up gravity. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	FVector WorldUp = FVector::UpVector;

	/**
	 * The control rotation of the locally-controlled controller (look input). Modes that follow
	 * player look (third/first person, orbit) build their view from this; framing modes ignore it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	FRotator ControlRotation = FRotator::ZeroRotator;

	/** The view produced last frame (post-blend), so a mode can ease from where the camera actually is. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	FVector PreviousCameraLocation = FVector::ZeroVector;

	/** As PreviousCameraLocation, for rotation. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Camera")
	FRotator PreviousCameraRotation = FRotator::ZeroRotator;
};

/**
 * The output of a single camera mode for one frame: a desired POV (location, rotation, FOV).
 * The stack blends several of these by weight into the final view applied to the camera manager.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_CameraView
{
	GENERATED_BODY()

	/** Desired camera world location. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Camera")
	FVector Location = FVector::ZeroVector;

	/** Desired camera world rotation. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Camera")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Desired horizontal field of view in degrees. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Camera")
	float FOV = 90.f;

	/** Component-wise linear interpolation between two views by Alpha (rotation via shortest-arc Lerp). */
	static FCam_CameraView Blend(const FCam_CameraView& A, const FCam_CameraView& B, float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f);
		FCam_CameraView Out;
		Out.Location = FMath::Lerp(A.Location, B.Location, T);
		// Use quaternion shortest-arc interpolation to avoid gimbal/wrap artifacts, then back to rotator.
		Out.Rotation = FQuat::Slerp(A.Rotation.Quaternion(), B.Rotation.Quaternion(), T).Rotator();
		Out.FOV = FMath::Lerp(A.FOV, B.FOV, T);
		return Out;
	}
};

/**
 * Strategy base for a single camera behaviour (third-person follow, first-person, top-down, orbit,
 * fixed, …). An instanced, EditInlineNew UObject so designers compose modes inline on the director /
 * settings and tune their UPROPERTY knobs with no code. Abstract: ship concrete subclasses.
 *
 * Modes are PURE behaviours: given a read-only FCam_ViewContext they emit a desired FCam_CameraView.
 * They hold no authority state, are never replicated, and never touch the APlayerCameraManager
 * directly — the director owns the stack, blends mode outputs, and applies the result via a single
 * UCameraModifier so it cooperates with (rather than fights) the engine camera pipeline.
 */
UCLASS(EditInlineNew, Blueprintable, Abstract, CollapseCategories)
class DESIGNPATTERNSCAMERA_API UCam_CameraMode : public UObject
{
	GENERATED_BODY()

public:
	UCam_CameraMode();

	/**
	 * Compute this mode's desired view for the frame.
	 * @param DeltaTime  Frame delta (seconds). Modes use it for lag/easing — never for authority logic.
	 * @param Context    Read-only per-frame inputs (view target, pivot, control rotation, previous view).
	 * @param OutView    Filled with this mode's desired location/rotation/FOV.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Mode")
	void UpdateView(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView);
	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView);

	/**
	 * Called once when the stack first pushes this mode (after instancing). Lets a mode seed any
	 * transient runtime state from the current context. Default: snapshot pivot for smoothing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Camera|Mode")
	void OnEnterStack(const FCam_ViewContext& Context);
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context);

	/** Called when the mode is popped from the stack (before it stops being evaluated). */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Camera|Mode")
	void OnExitStack();
	virtual void OnExitStack_Implementation();

	/** The identity tag a mode reports (mirrors its settings mapping); used for GetActiveModeTag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Mode")
	FGameplayTag GetModeTag() const { return ModeTag; }

	/** Set by the stack from the settings mapping when the mode is instanced. */
	void SetModeTag(const FGameplayTag InTag) { ModeTag = InTag; }

	/** Current blend weight in [0,1] this mode contributes to the final view. Owned by the stack. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Mode")
	float GetBlendWeight() const { return BlendWeight; }

	/** Stack-only setter for the live blend weight. */
	void SetBlendWeight(const float InWeight) { BlendWeight = FMath::Clamp(InWeight, 0.f, 1.f); }

	/** Seconds to blend IN when this mode becomes the top of the stack. */
	float GetBlendInTime() const { return BlendInTime; }

	/** Seconds to blend OUT when this mode is no longer the top of the stack. */
	float GetBlendOutTime() const { return BlendOutTime; }

protected:
	/**
	 * Identity tag (e.g. Cam.Mode.ThirdPerson). Authored on the settings mapping rather than
	 * hand-set here so one mode class can be reused under several tags if desired.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mode")
	FGameplayTag ModeTag;

	/**
	 * Seconds to ease this mode in when it gains weight. Tunable, not magic: a fast snap is 0,
	 * a cinematic ease is larger. Defensive default chosen for responsive gameplay cameras.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0", Units = "s"))
	float BlendInTime = 0.4f;

	/** Seconds to ease this mode out when it loses weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0.0", Units = "s"))
	float BlendOutTime = 0.4f;

	/** Live blend weight maintained by the stack; not edited by designers. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Blend")
	float BlendWeight = 0.f;

	/**
	 * Exponential smoothing helper: critically-damped-ish lerp toward Target with a rate that is
	 * frame-rate independent. Lag <= 0 disables smoothing (returns Target). Shared by follow modes.
	 */
	static FVector SmoothVector(const FVector& Current, const FVector& Target, float Lag, float DeltaTime);
	static FRotator SmoothRotator(const FRotator& Current, const FRotator& Target, float Lag, float DeltaTime);
};
