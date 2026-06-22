// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Flow/Seam_FlowGuard.h"
#include "GameplayTagContainer.h"
#include "Flow_ProfileLoadedGuard.generated.h"

/**
 * Built-in flow guard the GameFlow module ships and registers under FlowTags::Service_FlowGuard. It
 * vetoes any transition INTO active gameplay (Flow.Phase.InGame or a child) unless a player profile has
 * been loaded — read through the shared ISeam_SaveSlotManager (a most-recent slot exists) so the guard
 * stays genre-agnostic and does not depend on the SaveSystem module's concrete type.
 *
 * Genre-agnostic and pure-read: it only observes the save-slot seam; it never mutates anything. A project
 * that wants to allow entering gameplay without a profile (e.g. a fresh new-game flow) simply leaves the
 * profile available or unregisters this guard. The veto applies only on validated (non-forced)
 * transitions, so ForceTransition error-recovery is never blocked.
 *
 * Owned as a UPROPERTY subobject of the flow subsystem (NewObject(Outer)) so the locator can observe it
 * WeakObserved without leaking; resolves the save-slot seam through the locator each call (re-resolved,
 * never cached, so a hot-swapped provider is honoured).
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_ProfileLoadedGuard : public UObject, public ISeam_FlowGuard
{
	GENERATED_BODY()

public:
	//~ Begin ISeam_FlowGuard
	/** Denies a transition into InGame when no profile/most-recent slot is available. */
	virtual bool CanTransition_Implementation(FGameplayTag From, FGameplayTag To, FGameplayTag& OutDenyReason) const override;
	//~ End ISeam_FlowGuard

	/**
	 * When true (default), this guard is active. A project can disable the shipped guard without
	 * unregistering it by toggling this (e.g. via the owning subsystem) — defensive convenience.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow|Guard")
	bool bEnabled = true;

private:
	/** True if a player profile is currently available (a most-recent save slot exists). */
	bool IsProfileAvailable() const;
};
