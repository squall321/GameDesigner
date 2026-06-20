// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimAg_BrainTypes.generated.h"

/**
 * Where a brain strategy's chosen move target comes from. Lets a designer point a strategy at the
 * agent's home/work anchors (read via the agent seam) or at an explicit world point, with no code edit.
 */
UENUM(BlueprintType)
enum class ESimAg_NeedTargetSource : uint8
{
	/** Use the agent seam's GetHomeLocation(). */
	Home,
	/** Use the agent seam's GetWorkLocation(). */
	Work,
	/** Use the strategy's TargetOverride field. */
	ExplicitTarget,
	/** Stay where the agent already is (the strategy is satisfied in place). */
	CurrentLocation
};

/**
 * Shared blackboard key names the brain strategies and steering agree on. Centralised so the brain
 * writes the same keys the steering component reads. These are defaults; each strategy/component still
 * exposes the key as an editable FName for project-specific conventions.
 */
namespace SimAg_BrainKeys
{
	/** FVector: the chosen world move target. */
	const FName MoveTarget = TEXT("MoveTarget");

	/** Bool: whether the agent currently has a move goal. */
	const FName IsMoving = TEXT("IsMoving");

	/** Object: the live job handle is not storable; the claimed job id is mirrored onto the agent. */
	const FName ClaimedJobActive = TEXT("HasJob");
}
