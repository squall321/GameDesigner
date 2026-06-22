// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_Types.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Command/DPCommandContext.h"     // FDP_CommandTargetHandle (net-safe durable target identity)
#include "Interact_InteractorComponent.generated.h"

class UInteract_FocusStrategy;
class UInteract_QueryShapeStrategy;

/** Local delegate: the focused interactable changed (fires on the owning client). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInteract_OnFocusChanged,
	AActor*, NewFocusActor, const FInteract_PromptInfo&, Prompt);

/** Local delegate: an interaction the server confirmed has started for this interactor. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInteract_OnInteractStarted,
	AActor*, TargetActor, FGameplayTag, Verb);

/** Local delegate: an interaction this interactor was running has completed (with a result/reason). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FInteract_OnInteractCompleted,
	AActor*, TargetActor, FGameplayTag, Verb, EInteract_EndReason, Reason);

/**
 * Player-owned interaction driver placed on a controllable pawn.
 *
 * RESPONSIBILITIES
 *   - Each tick on the OWNING client it detects nearby interactables (overlap bounded by the
 *     detection params, narrowed by a cone, optionally line-of-sight gated), runs the configured
 *     focus strategy to pick one, and exposes the current focus + prompt locally (OnFocusChanged).
 *   - On a local interact request it predicts nothing authoritative — it sends a server RPC with
 *     only the desired VERB (never a client-named target). The SERVER re-runs detection from the
 *     pawn's own (server-side) view, re-validates range/LOS/CanInteract, and only then calls the
 *     interactable's authority-only BeginInteract. This closes the "trust the client's target"
 *     hole entirely: the client cannot name an actor it could not legitimately reach.
 *   - The server broadcasts DP.Bus.Interact.{Begin,Complete,Cancel} (FInstancedStruct payload) and
 *     replies to the owning client via ClientInteractResult for UI feedback.
 *
 * REPLICATION
 *   - Replicated (component default on). The only replicated state is the currently-active verb +
 *     its start time, ReplicatedUsing OnRep, COND_OwnerOnly (only the owner needs to see its own
 *     interaction progress for hold bars). The component is NOT a replicated authority carrier for
 *     interactable state — that lives on the interactables themselves.
 *   - Every mutator of ActiveVerb/ActiveTarget guards authority at the top and early-returns on
 *     clients.
 */
UCLASS(ClassGroup = (DesignPatternsInteraction), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSINTERACTION_API UInteract_InteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteract_InteractorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Public local API (owning client) ----

	/**
	 * Request an interaction with the currently-focused interactable using DesiredVerb (empty =
	 * the focus target's default verb). Routes to the server via ServerInteract. Safe to call on the
	 * owning client; a no-op with NoTarget feedback if there is no current focus.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RequestInteract(FGameplayTag DesiredVerb);

	/**
	 * Request ending the in-progress interaction (e.g. releasing a hold). Routes to the server,
	 * which calls EndInteract on the active interactable with the given reason.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RequestEndInteract(EInteract_EndReason Reason);

	/**
	 * ADDITIVE. Request an interaction with a SPECIFIC client-named target (e.g. a cursor pick or a
	 * cycled candidate that is not the focus-strategy default). Unlike RequestInteract, this carries
	 * the target as a net-safe durable handle so the SERVER can re-validate that exact candidate
	 * (resolve, CanInteract, range/LOS, availability seam) before BeginInteract — the client cannot
	 * name an actor it could not legitimately reach. Routes to ServerInteractAt on remote clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RequestInteractAt(FGameplayTag DesiredVerb, AActor* Target);

	/**
	 * ADDITIVE. Request a batch ("interact with all") for DesiredVerb: the server re-derives every
	 * eligible candidate, clamps to MaxBatchTargets, and runs BeginInteract on each accepted one,
	 * broadcasting one DP.Bus.Interact.BatchComplete. Routes to ServerBatchInteract on remote clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void RequestBatchInteract(FGameplayTag DesiredVerb);

	/**
	 * ADDITIVE. Build the full multi-verb menu (every supported verb folded through the availability
	 * seam) for the currently-focused interactable. Empty when there is no focus. Local/UI helper.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void GetFocusVerbMenu(FInteract_VerbMenu& Out) const;

	/** The actor currently focused locally (the focus strategy's pick), or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	AActor* GetFocusActor() const { return FocusActor.Get(); }

	/** The prompt for the currently-focused interactable (empty/disabled when no focus). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	const FInteract_PromptInfo& GetFocusPrompt() const { return FocusPrompt; }

	/** True if an interaction is currently active (replicated to the owner). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	bool IsInteracting() const { return ActiveVerb.IsValid(); }

	/** The verb of the active interaction (replicated to the owner), or empty. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	FGameplayTag GetActiveVerb() const { return ActiveVerb; }

	/** Server world time the active interaction started (replicated to the owner). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	double GetActiveStartServerTime() const { return ActiveStartServerTimeSeconds; }

	/**
	 * Hold progress in [0,1] for the active verb if it is a hold verb, else 0. Computed locally from
	 * the replicated start time against the verb definition's HoldSeconds. Returns 1 for non-hold
	 * verbs while active.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact")
	float GetActiveHoldProgress() const;

	// ---- Delegates (local) ----

	/** Fires on the owning client whenever the focused interactable changes. */
	UPROPERTY(BlueprintAssignable, Category = "Interact")
	FInteract_OnFocusChanged OnFocusChanged;

	/** Fires when the server confirms an interaction has started for this interactor. */
	UPROPERTY(BlueprintAssignable, Category = "Interact")
	FInteract_OnInteractStarted OnInteractStarted;

	/** Fires when an interaction this interactor was running ends. */
	UPROPERTY(BlueprintAssignable, Category = "Interact")
	FInteract_OnInteractCompleted OnInteractCompleted;

	// ---- Server-side authority API (no RPC; called by the server after re-validation) ----

	/**
	 * AUTHORITY ONLY. Re-derive the best interactable from the server-side view, validate it for
	 * Verb, and begin the interaction. Called by ServerInteract_Implementation; also callable from
	 * server gameplay code. Returns the outcome.
	 */
	EInteract_Result ServerBeginInteractAuthoritative(FGameplayTag DesiredVerb);

	/**
	 * AUTHORITY ONLY. End the active interaction (if any) with Reason and broadcast the matching
	 * bus event. Called by ServerEndInteract_Implementation and on interruptions.
	 */
	void ServerEndInteractAuthoritative(EInteract_EndReason Reason);

	/**
	 * ADDITIVE / AUTHORITY ONLY. Re-validate a CLIENT-NAMED target (resolved from a durable handle) for
	 * Verb and begin the interaction. Re-runs detection to confirm the named candidate is one the
	 * instigator can actually reach (CanInteract + range/LOS + availability seam), so a cursor/cycler
	 * pick can never bypass authority. Called by ServerInteractAt_Implementation; also callable from
	 * server gameplay code. Returns the outcome.
	 */
	EInteract_Result ServerBeginInteractAtAuthoritative(FGameplayTag DesiredVerb, AActor* Target);

	/**
	 * ADDITIVE / AUTHORITY ONLY. Re-derive ALL eligible candidates for Verb, clamp to MaxBatchTargets,
	 * BeginInteract each accepted one (instant verbs complete immediately), and broadcast a single
	 * DP.Bus.Interact.BatchComplete. Returns the number of targets successfully interacted with.
	 * Called by ServerBatchInteract_Implementation and the batch command.
	 */
	int32 ServerBatchInteractAuthoritative(FGameplayTag DesiredVerb);

	/**
	 * ADDITIVE. Snapshot the locally-detected candidate actors (in detection order) for this tick.
	 * Used by UInteract_FocusCyclerComponent to cycle through reachable targets. Local/cosmetic; the
	 * server still re-validates any chosen target. Fills OutActors (cleared first).
	 */
	void GetLocalCandidateActors(TArray<AActor*>& OutActors) const;

	/** ADDITIVE. Override the local focus to a specific candidate actor (focus cycling / cursor pick). */
	void SetLocalFocusOverride(AActor* OverrideActor);

	/** ADDITIVE. Clear any local focus override, returning to the focus strategy's pick. */
	void ClearLocalFocusOverride();

protected:
	// ---- Tunables / configuration ----

	/** Detection parameters (range, cone, channel, LOS) used by both client focus and server re-derive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Config")
	FInteract_DetectionParams Detection;

	/**
	 * The focus strategy used to pick a single interactable from the detected candidates. Inline
	 * instanced subobject — designers swap the concrete strategy in the details panel. Defaults to a
	 * line-of-sight strategy in the constructor.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Interact|Config")
	TObjectPtr<UInteract_FocusStrategy> FocusStrategy;

	/** How many times per second the OWNING client re-runs focus detection (throttles the trace cost). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Config", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float FocusUpdateHz = 15.f;

	/** Maximum interactables the detection overlap will consider per update (caps cost in dense scenes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Config", meta = (ClampMin = "1"))
	int32 MaxCandidates = 16;

	/**
	 * ADDITIVE. Optional broad-phase shape that gathers raw candidate actors before the component runs
	 * its existing per-candidate scoring. When null (default) the component uses its built-in sphere
	 * overlap path unchanged — full backward parity. Inline-instanced so designers swap it in details.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Interact|Config")
	TObjectPtr<UInteract_QueryShapeStrategy> QueryShape;

	/**
	 * ADDITIVE. Maximum number of targets a single batch ("interact with all") request will act on.
	 * The server clamps the re-derived candidate set to this, so a client cannot force an unbounded
	 * batch. Tunable, not a magic constant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Config", meta = (ClampMin = "1"))
	int32 MaxBatchTargets = 8;

private:
	// ---- Replicated state (owner-only) ----

	/** The verb of the active interaction, or empty when idle. Replicated to the owner only. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveVerb)
	FGameplayTag ActiveVerb;

	/** Server world time the active interaction began. Replicated to the owner only. */
	UPROPERTY(Replicated)
	double ActiveStartServerTimeSeconds = 0.0;

	/**
	 * Client-side shadow of the last-seen ActiveVerb, used by OnRep to detect start/end transitions
	 * without relying on the typed "previous value" parameter (which only one property in a shared
	 * OnRep would receive). Not replicated.
	 */
	UPROPERTY(Transient)
	FGameplayTag LastObservedActiveVerb;

	/** OnRep for ActiveVerb: fires the started/completed delegates locally on the owning client. */
	UFUNCTION()
	void OnRep_ActiveVerb();

	// ---- Server-side authoritative (non-replicated) bookkeeping ----

	/** The interactable object currently being interacted with on the server. Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> ActiveInteractableObject;

	/** The target actor of the active interaction on the server. Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> ActiveTargetActor;

	// ---- Local focus state (client) ----

	/** The locally focused actor (focus strategy pick). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> FocusActor;

	/** The locally focused interactable object (actor or component). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> FocusInteractableObject;

	/** Cached prompt for the focused interactable. */
	UPROPERTY(Transient)
	FInteract_PromptInfo FocusPrompt;

	/** Time accumulator (seconds) used to throttle focus updates to FocusUpdateHz. */
	float FocusAccumulator = 0.f;

	/**
	 * ADDITIVE. When valid, the local focus is forced to this actor (focus cycling / cursor pick)
	 * instead of the focus-strategy pick. Non-owning. Cleared via ClearLocalFocusOverride. Purely
	 * local — the server still re-validates whatever target an interaction request names.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LocalFocusOverride;

	/**
	 * ADDITIVE. The actors detected during the most recent local focus update, in detection order.
	 * Snapshotted so UInteract_FocusCyclerComponent can cycle through them without re-tracing. Local.
	 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> LocalCandidateActors;

	// ---- Server RPCs ----

	/**
	 * Client -> server intent. Carries ONLY the desired verb; the server re-derives the target by
	 * re-running detection and never trusts a client-named actor. Validation rejects requests from a
	 * non-owning pawn or with a malformed verb.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerInteract(FGameplayTag DesiredVerb);

	/** Client -> server intent to end the active interaction. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEndInteract(EInteract_EndReason Reason);

	/** Server -> owning client feedback with the outcome of a request (for UI). */
	UFUNCTION(Client, Reliable)
	void ClientInteractResult(EInteract_Result Result, FGameplayTag Verb);

	/**
	 * ADDITIVE. Client -> server intent naming a SPECIFIC target by a net-safe durable handle. The
	 * server resolves the handle, then re-validates that the named target is genuinely reachable
	 * (re-runs detection and matches the candidate) before acting — never trusting the client's reach.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerInteractAt(FGameplayTag DesiredVerb, FDP_CommandTargetHandle Target);

	/** ADDITIVE. Client -> server intent to interact with all eligible targets for a verb (clamped server-side). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerBatchInteract(FGameplayTag DesiredVerb);

	// ---- Helpers ----

	/** True if this machine is the server/authority for the owning actor. */
	bool HasAuthority() const;

	/** Build a query from the owner pawn's current view (camera/eyes + look direction). */
	FInteract_Query BuildQueryFromOwnerView(FGameplayTag DesiredVerb) const;

	/**
	 * Run detection: overlap within Range on Channel, narrow by the cone, optional LOS, and fill
	 * OutCandidates (capped at MaxCandidates). Used by both client focus and server re-derive so the
	 * two see the same geometry rules.
	 */
	void DetectCandidates(const FInteract_Query& Query, TArray<FInteract_Candidate>& OutCandidates) const;

	/** Resolve the IInteract_Interactable object on Actor (the actor itself or a component), or null. */
	UObject* FindInteractableOn(AActor* Actor) const;

	/** Re-run focus selection locally and update FocusActor/FocusPrompt, firing OnFocusChanged on change. */
	void UpdateLocalFocus();

	/**
	 * Resolve the verb a query should use against an interactable: DesiredVerb if supported, else the
	 * interactable's default (first supported) verb. Returns false if no usable verb exists.
	 */
	bool ResolveEffectiveVerb(UObject* Interactable, const FInteract_Query& Query, FGameplayTag& OutVerb) const;

	/** Current server world time (seconds), 0 if no world. */
	double GetServerTimeSeconds() const;

	/** Broadcast a DP.Bus.Interact.* event with an FInteract_BusPayload (server side). */
	void BroadcastBusEvent(FGameplayTag Channel, AActor* Target, FGameplayTag Verb, EInteract_EndReason Reason) const;

	/** ADDITIVE. Broadcast DP.Bus.Interact.Denied with an FInteract_DenialPayload (server side). */
	void BroadcastDenied(AActor* Target, FGameplayTag Verb, FGameplayTag ReasonTag) const;

	/** ADDITIVE. Broadcast DP.Bus.Interact.BatchComplete with an FInteract_BatchPayload (server side). */
	void BroadcastBatchComplete(FGameplayTag Verb, int32 SuccessCount) const;

	/**
	 * ADDITIVE. Score one already-gathered candidate actor for Query, filling OutCandidate. Shared by
	 * both the built-in sphere path and the QueryShape broad-phase so client and server compute
	 * identical metadata. Returns false if the actor is not a valid, eligible candidate.
	 */
	bool ScoreCandidateActor(AActor* Actor, const FInteract_Query& Query, FInteract_Candidate& OutCandidate) const;

	/**
	 * ADDITIVE. Check an interactable's availability via the optional ISeam_InteractAvailability seam
	 * for Verb against the owner's identity. Returns true (available) when the seam is absent. On a
	 * false result OutReasonTag carries the DP.Interact.Reason.* tag.
	 */
	bool CheckVerbAvailability(UObject* Interactable, FGameplayTag Verb, FGameplayTag& OutReasonTag) const;

	/** Set the replicated active-interaction pair on the server (authority-guarded). */
	void SetActiveInteraction_Server(FGameplayTag Verb, double StartTime, UObject* Interactable, AActor* TargetActor);

	/** Clear the replicated active-interaction pair on the server (authority-guarded). */
	void ClearActiveInteraction_Server();
};
