// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Flow_FlowHistory.generated.h"

/**
 * Re-entrancy lock + bounded phase back-stack layered on the flow FSM's ActivePhase/PreviousPhase.
 *
 * Two jobs:
 *  1) RE-ENTRANCY: bInTransition is set at the TOP of the subsystem's DoTransition on BOTH the forced and
 *     non-forced paths (re-entering a transition while one is in flight is always unsafe — a side effect
 *     that requests another transition would corrupt the FSM). The subsystem checks IsTransitioning()
 *     first and rejects re-entrant requests.
 *  2) BACK-STACK: a bounded history of entered phases so GoBack() can pop overlay phases
 *     (Pause -> InGame, NetError -> MainMenu) without the caller hard-coding the return phase. Depth is
 *     bounded (oldest entries drop) so the stack can never grow without limit.
 *
 * Pure LOCAL bookkeeping — never replicated, never saved. Owned as a UPROPERTY subobject of the flow
 * subsystem (NewObject(Outer)).
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_FlowHistory : public UObject
{
	GENERATED_BODY()

public:
	/** Set the maximum number of phases retained on the back-stack (clamped to >=1). */
	void SetMaxDepth(int32 InMaxDepth);

	/**
	 * Push Phase onto the back-stack (called by the subsystem after a committed transition). Drops the
	 * oldest entry if the stack exceeds the max depth. A repeated push of the current top is ignored so an
	 * idempotent transition does not bloat the stack.
	 */
	void PushPhase(FGameplayTag Phase);

	/**
	 * The phase to return to from the current one (the entry below the top), or an invalid tag if there is
	 * nothing to go back to. Does NOT mutate the stack — the subsystem performs the actual transition and
	 * the resulting PushPhase keeps the stack consistent.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|History")
	FGameplayTag PeekBackTarget() const;

	/** Pop the top entry off the stack and return the new top (the back target). Invalid if empty after pop. */
	FGameplayTag PopForBack();

	/** True if there is a phase below the current top to go back to. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|History")
	bool CanGoBack() const;

	/** Copy the current back-stack (oldest first) for diagnostics / a debug overlay. */
	UFUNCTION(BlueprintCallable, Category = "Flow|History")
	void GetHistory(TArray<FGameplayTag>& OutHistory) const;

	/** Clear the back-stack (e.g. on a hard error-recovery jump that invalidates the prior history). */
	void Reset();

	// --- Re-entrancy lock ---

	/** True while a transition is in progress (set at the top of DoTransition, cleared at the end). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|History")
	bool IsTransitioning() const { return bInTransition; }

	/**
	 * Try to BEGIN a transition: returns false if one is already in flight (re-entrant request rejected),
	 * otherwise sets the lock and returns true. The subsystem MUST pair a true result with EndTransition.
	 */
	bool BeginTransition();

	/** End the current transition, clearing the re-entrancy lock. Safe to call when not locked. */
	void EndTransition();

private:
	/** The bounded back-stack of entered phases (oldest first; the last entry is the current phase). */
	UPROPERTY(Transient)
	TArray<FGameplayTag> PhaseStack;

	/** Maximum retained depth (clamped >=1). */
	int32 MaxDepth = 16;

	/** Re-entrancy lock; true while inside a transition. */
	bool bInTransition = false;
};
