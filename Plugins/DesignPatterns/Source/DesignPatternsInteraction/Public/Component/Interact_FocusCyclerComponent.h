// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Interact_FocusCyclerComponent.generated.h"

class UInteract_InteractorComponent;

/** Local delegate: the set of cyclable candidates changed (e.g. the player moved). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInteract_OnCandidateListChanged, const TArray<AActor*>&, Candidates);

/**
 * LOCAL focus-cycling component placed beside UInteract_InteractorComponent on the player pawn.
 *
 * Lets the player step next/previous through ALL locally-detected candidates (read from the
 * interactor's per-tick candidate snapshot) and override the local focus to the cycled one via the
 * interactor's SetLocalFocusOverride. On a request-to-interact it routes through the interactor's
 * targeted ServerInteractAt path, so the server re-validates the cycled candidate's reachability —
 * cycling is purely a LOCAL focus hint and never an authority bypass.
 *
 * Local and never replicated.
 */
UCLASS(ClassGroup = (DesignPatternsInteraction), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Cooking))
class DESIGNPATTERNSINTERACTION_API UInteract_FocusCyclerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteract_FocusCyclerComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Step the local focus to the next detected candidate (wrapping). */
	UFUNCTION(BlueprintCallable, Category = "Interact|Cycle")
	void CycleNext();

	/** Step the local focus to the previous detected candidate (wrapping). */
	UFUNCTION(BlueprintCallable, Category = "Interact|Cycle")
	void CyclePrev();

	/** Clear any cycling override, returning focus to the interactor's focus strategy. */
	UFUNCTION(BlueprintCallable, Category = "Interact|Cycle")
	void ResetToStrategyPick();

	/** Request an interaction with the currently-cycled candidate (server re-validates). */
	UFUNCTION(BlueprintCallable, Category = "Interact|Cycle")
	void InteractWithCycled(FGameplayTag DesiredVerb);

	/** The actor currently selected by cycling (or null when reset to the strategy pick). */
	UFUNCTION(BlueprintPure, Category = "Interact|Cycle")
	AActor* GetCycledActor() const { return CycledActor.Get(); }

	/** Fires (locally) when the detected candidate set changes between ticks. */
	UPROPERTY(BlueprintAssignable, Category = "Interact|Cycle")
	FInteract_OnCandidateListChanged OnCandidatesChanged;

protected:
	/**
	 * How often (Hz) to refresh the cached candidate list from the interactor and emit
	 * OnCandidatesChanged when it differs. Tunable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Cycle", meta = (ClampMin = "1.0", ClampMax = "60.0"))
	float RefreshHz = 10.f;

private:
	/** Resolve the sibling interactor component on the owner. */
	UInteract_InteractorComponent* ResolveInteractor() const;

	/** Step the cycled index by Delta over the cached candidate list and push the override down. */
	void Step(int32 Delta);

	/** Refresh the cached candidate list; returns true if it changed. */
	bool RefreshCandidates();

	/** The interactor this cycler drives. Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInteract_InteractorComponent> Interactor;

	/** Cached candidate actors snapshot (kept in sync with the interactor each refresh). */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> CachedCandidates;

	/** The currently-cycled actor (null = follow the strategy). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> CycledActor;

	/** Accumulator (seconds) to throttle candidate-list refresh to RefreshHz. */
	float RefreshAccumulator = 0.f;
};
