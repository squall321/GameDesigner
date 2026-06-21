// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GM_MatchStateComponent.generated.h"

class UGM_RulesetDefinition;

/**
 * Fired (server and clients) when the match state tag changes - after replication on clients.
 * @param Component The match component whose state changed.
 * @param OldState  The previous match-state tag (invalid for the very first transition).
 * @param NewState  The new match-state tag.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGM_OnMatchStateChanged,
	UGM_MatchStateComponent*, Component, FGameplayTag, OldState, FGameplayTag, NewState);

/**
 * Replicated match-flow finite state machine, living on the GameState.
 *
 * This component owns the authoritative match lifecycle - WaitingToStart -> InProgress -> RoundOver ->
 * (next round) InProgress ... -> MatchOver - and replicates the active state tag to every client so HUD,
 * audio and game-flow react in sync. It deliberately holds the FSM as a single replicated FGameplayTag
 * (rather than spinning up a full UDP_StateMachineComponent) because match flow is a small fixed graph and
 * the carrier of record must be the replicated component, not a subsystem.
 *
 * AUTHORITY MODEL: every mutator guards HasAuthority() at the top and early-returns on clients. While the
 * match is InProgress the authority re-evaluates the active ruleset's win/lose conditions at the cadence
 * from UGM_DeveloperSettings (and immediately on explicit score/state pushes); when a lose-then-win check
 * resolves, it advances the FSM and asks the score subsystem to finalise results. Clients never evaluate
 * conditions - they only observe the replicated tag via OnRep and surface OnMatchStateChanged.
 *
 * The ruleset (a UGM_RulesetDefinition data asset) is assigned by the GameMode/GameState on the authority
 * and replicated so clients can present round/time info; it carries NO behaviour, only data.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentTick, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSGAMEMODE_API UGM_MatchStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGM_MatchStateComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** True on server / standalone / listen-server host (any net mode that is not a pure client). */
	bool HasMatchAuthority() const
	{
		const AActor* Owner = GetOwner();
		return Owner && Owner->HasAuthority();
	}

	// ---- Ruleset --------------------------------------------------------------------------------

	/**
	 * Assign the ruleset that drives this match. AUTHORITY ONLY; intended to be set once by the
	 * GameMode/GameState before (or at) match start. Replicated so clients can show round/time data.
	 * Passing null leaves the component in its documented inert mode (manual transitions, no auto win/lose).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	void SetRuleset(UGM_RulesetDefinition* InRuleset);

	/** The active ruleset (may be null). Resolved from the settings default when none was assigned. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	UGM_RulesetDefinition* GetRuleset() const;

	// ---- State queries (client-safe) ------------------------------------------------------------

	/** The current replicated match-state tag (one of GameModeNativeTags::Match_*). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	FGameplayTag GetMatchState() const { return MatchState; }

	/** True while the match is actively being played (state == InProgress). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	bool IsInProgress() const;

	/** True once the match is fully decided (state == MatchOver). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	bool IsMatchOver() const;

	/** The 1-based index of the current round (0 before the first round starts). Replicated. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	int32 GetCurrentRound() const { return CurrentRound; }

	/**
	 * Seconds the match has spent in the current InProgress segment (0 when not in progress). Derived
	 * from the replicated server start timestamp against the world clock, so clients get a consistent
	 * elapsed time. Used by the TimeElapsed ruleset condition.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	float GetElapsedInProgressSeconds() const;

	/** The winning key resolved at MatchOver (empty for a draw / not yet decided). Replicated. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Match")
	FGameplayTag GetWinningKey() const { return WinningKey; }

	// ---- Authority transitions (each early-returns on clients) ----------------------------------

	/**
	 * Begin the match: WaitingToStart -> InProgress, starting round 1 and seeding the in-progress clock.
	 * AUTHORITY ONLY. No-op if not currently WaitingToStart. @return true if the match started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	bool StartMatch();

	/**
	 * End the current round: InProgress -> RoundOver. AUTHORITY ONLY. If RoundOverIntermissionSeconds > 0
	 * the component auto-advances after the intermission; otherwise call AdvanceRound explicitly.
	 * @return true if a round was ended.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	bool EndRound();

	/**
	 * Advance from RoundOver to the next round (InProgress) or to MatchOver if the ruleset's RoundCount is
	 * reached. AUTHORITY ONLY. @return true if a transition happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	bool AdvanceRound();

	/**
	 * Force the match to MatchOver immediately, recording WinKey as the winner (empty = draw). AUTHORITY
	 * ONLY. Finalises scores through the score subsystem. @return true if the match ended here.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	bool EndMatch(FGameplayTag WinKey);

	/**
	 * Re-evaluate the active ruleset's conditions right now (authority only). Call after pushing score so
	 * a win is recognised without waiting for the next cadence tick. Safe no-op off authority / not in
	 * progress. @return true if the evaluation caused a state transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Match")
	bool EvaluateConditionsNow();

	/** Fired (server and clients) when the match state changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|GameMode|Match")
	FGM_OnMatchStateChanged OnMatchStateChanged;

private:
	/** Replicated current match-state tag. OnRep surfaces the change on clients. */
	UPROPERTY(ReplicatedUsing = OnRep_MatchState)
	FGameplayTag MatchState;

	/** Replicated 1-based current round number (0 before the first round). */
	UPROPERTY(Replicated)
	int32 CurrentRound = 0;

	/** Replicated winner resolved at MatchOver (empty = draw / undecided). */
	UPROPERTY(Replicated)
	FGameplayTag WinningKey;

	/**
	 * Replicated world-time (server clock) at which the current InProgress segment began. Negative means
	 * "not in progress". Clients compute elapsed = World->GetTimeSeconds() - this.
	 */
	UPROPERTY(Replicated)
	float InProgressStartServerTime = -1.f;

	/**
	 * The ruleset driving this match (assigned on authority, replicated for client-side round/time UI).
	 * UPROPERTY TObjectPtr so the GC and replication see it. May be null (inert mode).
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Ruleset)
	TObjectPtr<UGM_RulesetDefinition> Ruleset;

	/** Authority-side accumulator throttling condition evaluation to the settings cadence. */
	float ConditionEvalAccumulator = 0.f;

	/** Authority-side countdown for the RoundOver auto-advance intermission (negative = disabled). */
	float RoundOverCountdown = -1.f;

	/** OnRep: surface a match-state change and fire OnMatchStateChanged on clients. */
	UFUNCTION()
	void OnRep_MatchState(FGameplayTag OldState);

	/** OnRep: re-broadcast nothing structural; here for symmetry/logging when the ruleset arrives. */
	UFUNCTION()
	void OnRep_Ruleset();

	/**
	 * Core authority transition: set MatchState to NewState, do per-state bookkeeping (round/clock),
	 * broadcast OnMatchStateChanged locally and publish the match-state-changed bus message. AUTHORITY
	 * ONLY (callers guard). @return true if the state actually changed.
	 */
	bool TransitionTo(const FGameplayTag& NewState);

	/**
	 * Authority evaluation of the ruleset's lose-then-win condition lists against live world state.
	 * Drives RoundOver/MatchOver and the winning key. @return true if a transition happened.
	 */
	bool EvaluateRuleset();

	/** Resolve the effective ruleset: the assigned one, else the settings default (may be null). */
	UGM_RulesetDefinition* ResolveEffectiveRuleset() const;

	/** Broadcast the match-state-changed message on the bus (best-effort; null bus is a no-op). */
	void PublishStateChanged(const FGameplayTag& OldState, const FGameplayTag& NewState) const;
};
