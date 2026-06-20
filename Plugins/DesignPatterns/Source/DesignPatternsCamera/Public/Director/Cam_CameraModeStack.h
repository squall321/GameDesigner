// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_CameraModeStack.generated.h"

class UCurveFloat;

/**
 * One entry in the camera-mode stack: an instanced mode, its priority, its push request id,
 * and its live blend weight. The stack owns the instanced mode object via UPROPERTY so it is
 * GC-rooted while pushed.
 */
USTRUCT()
struct DESIGNPATTERNSCAMERA_API FCam_StackEntry
{
	GENERATED_BODY()

	/** The opaque request id returned to whoever pushed this mode; used to pop exactly this entry. */
	UPROPERTY()
	FGuid RequestId;

	/** Higher priority wins. Ties broken by push order (SerialOrder). */
	UPROPERTY()
	int32 Priority = 0;

	/** Monotonic push counter, the tie-breaker for equal priorities (later = on top). */
	UPROPERTY()
	uint64 SerialOrder = 0;

	/** The instanced mode behaviour. Owned (GC-rooted) by the stack while pushed. */
	UPROPERTY(Instanced)
	TObjectPtr<UCam_CameraMode> Mode = nullptr;

	/** Live blend weight target in [0,1] (1 for the current top mode, fading to 0 for others). */
	UPROPERTY()
	float TargetWeight = 0.f;

	/** Current (eased) blend weight in [0,1]. */
	UPROPERTY()
	float CurrentWeight = 0.f;

	bool IsValid() const { return Mode != nullptr && RequestId.IsValid(); }
};

/**
 * Priority/blend stack of instanced camera modes. The Composite/Strategy heart of the camera system:
 * any number of modes can be pushed; the stack picks the highest-priority "top" mode, eases its weight
 * toward 1 while easing every other mode toward 0, and produces a single blended FCam_CameraView each
 * frame by combining the active (weight > 0) modes top-down.
 *
 * Pure evaluation object — it never touches the engine camera; the director owns the stack and applies
 * its result through a UCameraModifier. Not replicated (camera is local/cosmetic).
 */
UCLASS()
class DESIGNPATTERNSCAMERA_API UCam_CameraModeStack : public UObject
{
	GENERATED_BODY()

public:
	UCam_CameraModeStack();

	/**
	 * Push an already-instanced mode at a priority and return its request id. The stack takes
	 * ownership (the mode's Outer should be this stack). OnEnterStack is called immediately.
	 * @return request id, or invalid FGuid if Mode is null.
	 */
	FGuid PushMode(UCam_CameraMode* Mode, int32 Priority, const FCam_ViewContext& SeedContext);

	/** Pop the entry with the given request id (calls OnExitStack). No-op if not found. */
	void PopMode(FGuid RequestId);

	/** Remove every entry (e.g. on teardown). Calls OnExitStack on each. */
	void ClearAll();

	/**
	 * Advance blend weights for DeltaTime and evaluate every active mode, returning the blended view.
	 * If the stack is empty, returns Fallback unchanged.
	 */
	FCam_CameraView EvaluateStack(float DeltaTime, const FCam_ViewContext& Context, const FCam_CameraView& Fallback);

	/** The tag of the current top mode (highest priority / latest push), or empty if the stack is idle. */
	FGameplayTag GetTopModeTag() const;

	/** The current top entry's mode object (may be null if idle). */
	UCam_CameraMode* GetTopMode() const;

	/** Number of currently-pushed modes. */
	int32 Num() const { return Entries.Num(); }

	/** True if no modes are pushed. */
	bool IsEmpty() const { return Entries.Num() == 0; }

	/**
	 * Optional blend-shaping curve mapping linear blend alpha [0,1] -> shaped alpha [0,1]. When null,
	 * a smoothstep ease is used. Set by the director from its developer settings.
	 */
	void SetBlendCurve(UCurveFloat* InCurve) { BlendCurve = InCurve; }

	/** One-line debug summary (top tag, entry count, weights) for the director's debug string. */
	FString BuildDebugString() const;

private:
	/** Resolve the index of the current top entry (highest Priority, then highest SerialOrder). INDEX_NONE if empty. */
	int32 FindTopIndex() const;

	/** Apply the blend curve (or default smoothstep) to a linear alpha. */
	float ShapeAlpha(float LinearAlpha) const;

	/** Active stack entries (unordered; top is computed by priority + serial). */
	UPROPERTY()
	TArray<FCam_StackEntry> Entries;

	/** Monotonic push counter used to break priority ties. */
	UPROPERTY()
	uint64 NextSerial = 1;

	/** Optional designer blend-shaping curve. Weak-style raw UPROPERTY ptr (asset, not owned here). */
	UPROPERTY()
	TObjectPtr<UCurveFloat> BlendCurve = nullptr;
};
