// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Save/DPSaveGameSubsystem.h"   // EDP_SaveResult (UFUNCTION callback signature)
#include "Flow_BootSequenceController.generated.h"

class UFlow_GameFlowSubsystem;
class UFlow_BootSequenceDefinition;
class UFlow_BootStepDefinition;
class UDP_MessageBusSubsystem;
class UDP_SaveGame;

/**
 * Runs the data-driven boot sequence while the FSM sits in Flow.Phase.Boot, then transitions to the
 * configured initial phase. Owned as a UPROPERTY(Transient) subobject of UFlow_GameFlowSubsystem
 * (NewObject(Outer)).
 *
 * For each UFlow_BootStepDefinition (in order) it runs the built-in side effect for the step's StepKind:
 *  - Legal:       pushes the step's screen and holds for MinSeconds.
 *  - Preload:     front-loads the step's soft refs through the EXISTING loading-screen subsystem, driving
 *                 the real progress fraction; gates until complete (if bGatesOnComplete).
 *  - ProfileLoad: loads the player profile via ISeam_SaveSlotManager's most-recent slot through the core
 *                 save subsystem; gates until done.
 *  - FirstRun:    runs only on the first launch (detected/cleared via the settings bHasCompletedFirstRun
 *                 config flag); skipped otherwise.
 *  - (unknown):   treated as a pure timed/preload step.
 *
 * A step advances when its work signals complete AND MinSeconds has elapsed, or when TimeoutSeconds
 * elapses (defensive). Driven by a low-frequency timer (no FTickable). On completion it calls the
 * subsystem's existing RequestTransition(InitialPhase). All timings/screens are designer data.
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_BootSequenceController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to the owning flow subsystem. */
	void Initialize(UFlow_GameFlowSubsystem* InOwner);

	/** Stop the boot timer + any in-flight preload (called from the owner's Deinitialize). */
	void Shutdown();

	/**
	 * Begin running the configured boot sequence. If no sequence is authored (or boot is skipped in PIE)
	 * it immediately transitions to the initial phase. Idempotent (a second call while running is ignored).
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Boot")
	void StartBoot();

	/** True once the whole boot sequence has completed and the initial-phase transition has been requested. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Boot")
	bool IsBootComplete() const { return bComplete; }

	/** True if this is the first launch (read from the settings first-run config flag). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Boot")
	bool IsFirstRun() const;

	/** Index of the currently-active step (INDEX_NONE before start / after completion). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Boot")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

private:
	/** Resolve the configured boot sequence (loaded synchronously; tiny asset), or null. */
	UFlow_BootSequenceDefinition* ResolveSequence() const;

	/** Begin the step at CurrentStepIndex (push screen, kick its work, reset the step timer). */
	void EnterStep();

	/** Timer callback: check the active step's completion + min-time, advance or finish. */
	void TickStep();

	/** Advance to the next applicable step (skipping first-run-only steps when not first run). */
	void AdvanceStep();

	/** Finish the sequence: clear the first-run flag, broadcast completion, transition to the initial phase. */
	void CompleteBoot();

	/** True if the active step's work has signalled complete (preload done / profile loaded / timed). */
	bool IsActiveStepWorkComplete() const;

	/** Broadcast the current boot-step state on DP.Bus.Flow.BootStepChanged. */
	void BroadcastStep(bool bCompleteNow);

	/** Resolve the owning GameInstance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Dynamic-delegate callback fired when the profile-load step's LoadAsync completes. */
	UFUNCTION()
	void HandleProfileLoaded(const FString& Slot, EDP_SaveResult Result, UDP_SaveGame* SaveObject);

	// --- State ---

	/** Owning flow subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlow_GameFlowSubsystem> Owner;

	/** The resolved boot sequence (kept alive while booting). */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_BootSequenceDefinition> Sequence;

	/** Index of the active step. */
	int32 CurrentStepIndex = INDEX_NONE;

	/** Seconds the active step has been running (for MinSeconds / TimeoutSeconds checks). */
	float StepElapsed = 0.f;

	/** True once a profile-load step has finished (set by its completion). */
	bool bProfileLoadDone = false;

	/** True while a profile-load is in flight for the active step. */
	bool bProfileLoadInFlight = false;

	/** True once the whole sequence has completed. */
	bool bComplete = false;

	/** True while the controller is actively running the sequence (guards StartBoot re-entry). */
	bool bRunning = false;

	/** Timer handle driving the step ticker. */
	FTimerHandle StepTimer;

	/** Interval the step timer fires at (derived from a small fixed boot cadence). */
	float StepTickInterval = 0.1f;
};
