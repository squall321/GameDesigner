// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Identity/Seam_EntityId.h"
#include "FSM/DPBlackboard.h"
#include "Cam_TargetingTypes.generated.h"

/**
 * One evaluated targeting candidate.
 *
 * Built by the targeting component from an overlap/trace result. Carries a stable id (for the
 * read seam / replication intent) PLUS the transient geometric facts a selection strategy needs to
 * score it. The raw actor is held WEAK and is used only inside one synchronous selection pass — the
 * id is the durable handle that leaves this module.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_TargetCandidate
{
	GENERATED_BODY()

	/** Stable id of the candidate, read from its ISeam_EntityIdentity. Invalid if it has none. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FSeam_EntityId EntityId;

	/** The candidate actor, weak — valid only for the duration of one selection pass. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	TWeakObjectPtr<AActor> Actor;

	/** World-space focus point used for framing/scoring (typically the candidate's target location). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FVector FocusLocation = FVector::ZeroVector;

	/** Distance (cm) from the viewer to FocusLocation. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	float Distance = 0.f;

	/** Angle (degrees) between the view forward and the direction to the candidate. 0 = dead ahead. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	float AngleFromViewDeg = 0.f;

	/** Archetype tag from the candidate's identity seam, for type-aware scoring/filtering. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FGameplayTag ArchetypeTag;

	FCam_TargetCandidate() = default;
};

/**
 * Read-only view of the viewer's framing situation, handed to a selection strategy alongside each
 * candidate so scoring stays pure (no reaching back into the component).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_TargetingView
{
	GENERATED_BODY()

	/** World-space eye/view location of the viewer. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FVector ViewLocation = FVector::ZeroVector;

	/** Normalized view forward direction. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FVector ViewForward = FVector::ForwardVector;

	/** Max acquisition range (cm) the component used for this pass; strategies may normalize by it. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	float MaxRange = 0.f;

	/** Half-angle (deg) of the acquisition cone the component used for this pass. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	float HalfAngleDeg = 0.f;

	/** The currently-locked target id (Invalid if none), so a strategy can add stickiness/hysteresis. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FSeam_EntityId CurrentTargetId;

	/**
	 * Optional shared blackboard provider (the IDP_BlackboardProvider seam). The highest-threat
	 * strategy reads per-entity threat from here, keeping AIModule / any threat system out of this
	 * module. Null when the project has no blackboard; strategies must degrade gracefully.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	TScriptInterface<IDP_BlackboardProvider> Blackboard;

	FCam_TargetingView() = default;
};

/**
 * Payload broadcast on DP.Bus.Camera.TargetChanged when the locked target changes.
 *
 * Cosmetic/UI-facing (HUD reticle, soft-lock prompt). Carries only the stable ids — never a raw
 * actor — so any listener works off the read seam. NOT replicated (re-derived locally per machine).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_TargetChangedEvent
{
	GENERATED_BODY()

	/** The newly-locked target id (Invalid when lock cleared). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FSeam_EntityId NewTargetId;

	/** The previously-locked target id (Invalid when there was none). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	FSeam_EntityId PreviousTargetId;

	/** True if this change was a hard lock-on toggle, false for soft re-acquisition. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Targeting")
	bool bHardLock = false;

	FCam_TargetChangedEvent() = default;
};
