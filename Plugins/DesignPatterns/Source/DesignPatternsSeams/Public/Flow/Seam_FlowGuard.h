// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_FlowGuard.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_FlowGuard : public UInterface
{
	GENERATED_BODY()
};

/**
 * Pluggable VETO seam over the top-level flow FSM. A guard answers "is this transition allowed right
 * now?" for a single (From -> To) phase edge, and on a denial returns a reason tag the flow can surface
 * (a screen / toast / log).
 *
 * Owned by the GameFlow module, which ships one built-in guard (UFlow_ProfileLoadedGuard) and consults
 * every registered guard ONLY on a validated (non-forced) transition — ForceTransition keeps its
 * documented bypass so hard error-recovery jumps (to Main Menu / a NetError phase) are never blocked by
 * a guard. Projects add more guards (e.g. "no quitting mid-tutorial", "no entering a match without a
 * party") by implementing this seam on any UObject and registering it under FlowTags::Service_FlowGuard.
 *
 * Multiple guards compose with AND semantics: a transition is permitted only if EVERY registered guard
 * allows it; the first denier's reason tag is the one reported. Guards must be PURE READS — they observe
 * already-decided state and never mutate it; the flow may call CanTransition many times (e.g. from
 * CanEnterPhase as well as the transition itself), so a guard with side effects would be a bug.
 *
 * The signature references only Core/GameplayTags so DesignPatternsSeams stays a dependency-free leaf.
 */
class DESIGNPATTERNSSEAMS_API ISeam_FlowGuard
{
	GENERATED_BODY()

public:
	/**
	 * True if a transition From -> To is permitted by this guard. On a denial, returns false and writes a
	 * reason tag into OutDenyReason (anchored under DP.Flow.Guard.Reason.* by convention) so the flow can
	 * present it. The defensive default (no project override) ALLOWS the transition, so an unimplemented
	 * guard never deadlocks the flow.
	 *
	 * @param From          The phase being left (invalid on the very first transition from no-phase).
	 * @param To            The phase being entered.
	 * @param OutDenyReason Filled with a reason tag when the result is false; left untouched when true.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Flow")
	bool CanTransition(FGameplayTag From, FGameplayTag To, FGameplayTag& OutDenyReason) const;
};
