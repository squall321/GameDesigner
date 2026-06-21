// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Prediction/FNet_PredictedState.h"
#include "UNet_PredictedStateComponent.generated.h"

/**
 * Fired (owning client) after a reconciliation snapshot arrives and is processed. bCorrected is true
 * when the server state diverged from the local prediction and the component snapped + re-simulated.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnReconciled, bool, bCorrected);

/**
 * Fired whenever the locally-applied predicted state changes (server and owning client), so cosmetic
 * presenters can react. Carries the current authoritative-or-predicted state value.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnPredictedStateChanged, FSeam_NetValue, NewState);

/**
 * GENERIC client-side prediction component implementing the canonical predict -> send -> reconcile loop
 * WITHOUT being tied to character movement (so it works for ability charge, vehicle state, ammo, custom
 * locomotion, etc.). It is deliberately state-type-agnostic: the owning game supplies the simulation step
 * by overriding the SimulateStep BlueprintNativeEvent (or binding in native), and encodes its state/input
 * as FSeam_NetValue.
 *
 * THE LOOP (mirrors the engine's CharacterMovement client prediction, generalized):
 *   1. On the OWNING client each frame: SubmitInput(Payload, DeltaTime) -> applies SimulateStep LOCALLY
 *      to PredictedState immediately (no input lag), stamps a monotonic Sequence, buffers the input in a
 *      bounded ring, and forwards it to the server via ServerSubmitInput (reliable, validated).
 *   2. On the SERVER: ServerSubmitInput validates the sequence is monotonic and the payload sane, applies
 *      SimulateStep to the AUTHORITATIVE state, advances AckedSequence, and replicates an
 *      FNet_PredictedSnapshot{AckedSequence, State} back (COND_OwnerOnly).
 *   3. On the OWNING client: OnRep_Snapshot discards acked inputs from the ring, compares the snapshot
 *      State against what the client predicted at AckedSequence; if they diverge beyond ReconcileTolerance
 *      it SNAPS PredictedState to the server State and RE-SIMULATES every still-unacked input forward.
 *
 * HARD OWNERSHIP GUARD (anti-cheat / correctness): prediction only runs when the owner is an autonomous
 * proxy on a net-connection-owning actor. If SubmitInput is called on a non-owning / simulated context it
 * logs ONCE and falls back to server-authoritative-only application (no client RPC), so a mis-attached
 * component can never inject inputs it doesn't own.
 *
 * Replication surface is intentionally tiny: a single OwnerOnly snapshot. No input history is replicated.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_PredictedStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_PredictedStateComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Public API (the single entry point gameplay code uses) ----

	/**
	 * Submit one frame of input. On the owning client: applies it locally (instant feedback), buffers it,
	 * and forwards to the server. On the server (listen/standalone owner): applies it authoritatively. On a
	 * non-owning context: logs once and no-ops (fail-closed). DeltaTime should be the frame's simulated dt.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Prediction")
	void SubmitInput(const FSeam_NetValue& Payload, float DeltaTime);

	/** The current locally-applied state (predicted on the owning client, authoritative on the server). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Prediction")
	FSeam_NetValue GetPredictedState() const { return PredictedState; }

	/** Number of inputs buffered but not yet acked by the server (the prediction "depth"). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Prediction")
	int32 GetPendingInputCount() const { return PendingInputs.Num(); }

	/** The highest input sequence the server has acknowledged for this component. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Prediction")
	int32 GetLastAckedSequence() const { return (int32)LastAckedSequence; }

	/**
	 * The pure simulation step: given an input applied for DeltaTime over the InState, produce the next
	 * state. MUST be deterministic (same inputs -> same output) so client re-simulation matches the server,
	 * and MUST NOT have side effects (it runs many times during reconciliation). Override per game.
	 * Default echoes InState (identity) so an unconfigured component is inert rather than crashing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Net|Prediction")
	FSeam_NetValue SimulateStep(const FSeam_NetValue& InState, const FSeam_NetValue& Input, float DeltaTime) const;
	virtual FSeam_NetValue SimulateStep_Implementation(const FSeam_NetValue& InState, const FSeam_NetValue& Input, float DeltaTime) const;

	/**
	 * Server-side input sanity validation, separate from sequence monotonicity (which the RPC enforces).
	 * Override to reject impossible payloads (out-of-range axes, illegal buttons). Default accepts any set
	 * value. Runs ON THE SERVER inside ServerSubmitInput. A rejection drops the input and logs.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Net|Prediction")
	bool ValidateServerInput(const FNet_PredictedInput& Input) const;
	virtual bool ValidateServerInput_Implementation(const FNet_PredictedInput& Input) const;

	// ---- Events ----

	/** Fired on the owning client each time a reconciliation snapshot is processed. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Prediction")
	FNet_OnReconciled OnReconciled;

	/** Fired (server + owning client) when the applied predicted state changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Prediction")
	FNet_OnPredictedStateChanged OnPredictedStateChanged;

	// ---- Tuning (data-driven; no hardcoded gameplay numbers) ----

	/**
	 * Maximum buffered (unacked) inputs. Beyond this the oldest are dropped (the client is too far ahead
	 * of the server — usually a stall). Bounds memory and RPC catch-up cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Prediction", meta = (ClampMin = "8", ClampMax = "512"))
	int32 MaxPendingInputs = 128;

	/**
	 * Reconciliation tolerance. Snapshot State is treated as "matching" the local prediction when their
	 * scalar/vector difference is within this epsilon, so floating-point jitter does not cause a snap.
	 * Compared component-wise for vectors and as an absolute difference for scalars.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Prediction", meta = (ClampMin = "0.0"))
	float ReconcileTolerance = 1.0f;

protected:
	/**
	 * Server RPC carrying one client input. Reliable + validated. The _Validate enforces a non-degenerate
	 * payload and a sane delta-time; the _Implementation enforces sequence monotonicity, runs the
	 * game-defined ValidateServerInput, applies the authoritative SimulateStep and advances the snapshot.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSubmitInput(FNet_PredictedInput Input);

private:
	/** Compare two net-values for reconciliation purposes, within ReconcileTolerance. */
	bool StatesMatch(const FSeam_NetValue& A, const FSeam_NetValue& B) const;

	/** True if this component's owner is a net-owning autonomous proxy (the predictor). */
	bool IsPredictingOwner() const;

	/** Apply Input to PredictedState locally and fire the change delegate. */
	void ApplyLocalStep(const FNet_PredictedInput& Input);

	/** OnRep handler: discard acked inputs, reconcile, re-simulate the remainder. */
	UFUNCTION()
	void OnRep_Snapshot();

	/** Set PredictedState and broadcast the change if it actually differs. */
	void SetPredictedState(const FSeam_NetValue& NewState);

	/**
	 * The locally-applied state. On the owning client this is the PREDICTED state (ahead of the server);
	 * on the server this is the AUTHORITATIVE state. Not directly replicated — the snapshot is.
	 */
	UPROPERTY(Transient)
	FSeam_NetValue PredictedState;

	/**
	 * The server's authoritative reconciliation snapshot, replicated to the owner only. OnRep drives the
	 * client reconcile. A single small struct is the ENTIRE replicated surface of this component.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FNet_PredictedSnapshot Snapshot;

	/** Bounded ring of inputs applied locally but not yet acked by the server (owning client only). */
	UPROPERTY(Transient)
	TArray<FNet_PredictedInput> PendingInputs;

	/** Next sequence number to stamp on a client input (monotonic, owning client only). */
	uint32 NextSequence = 1;

	/** Highest sequence the server has applied (authoritative on server; mirrors Snapshot on client). */
	uint32 LastAckedSequence = 0;

	/** Set once after the first non-owning misuse, so the warning logs exactly one time. */
	bool bLoggedOwnershipFallback = false;
};
