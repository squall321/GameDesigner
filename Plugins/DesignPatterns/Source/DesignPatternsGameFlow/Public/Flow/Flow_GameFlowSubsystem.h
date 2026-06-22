// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Flow/Seam_AppFlowController.h"
#include "GameplayTagContainer.h"
#include "Flow_GameFlowSubsystem.generated.h"

class UFlow_FlowStateDefinition;
class UDP_MessageBusSubsystem;
class UDP_ServiceLocatorSubsystem;
class UFlow_MatchmakingController;
class UFlow_TravelCoordinator;
class UFlow_PauseController;
class UFlow_BootSequenceController;
class UFlow_FlowHistory;
class UFlow_ProfileLoadedGuard;

/**
 * Top-level application/game flow finite-state machine (GameInstance-scoped so it survives level
 * travel) and the live implementation of the shared ISeam_AppFlowController.
 *
 * Responsibilities:
 *  - Holds the single ACTIVE flow phase tag (Boot/Title/MainMenu/Lobby/Loading/InGame/Pause/Results
 *    or a game's child phase). This is a top-level tag state machine, not the per-actor
 *    UDP_StateMachineComponent (a GI subsystem never replicates state; the flow is locally driven by
 *    each machine off already-decided input/UI, and authoritative level travel is server-driven).
 *  - On a transition: validates against the source phase's allowed-transition edge set, then drives
 *    side effects — level travel (UGameplayStatics::OpenLevel on a host / ClientTravel via the local
 *    player controller), pushes the phase's screen through the UI mediator by message bus, pushes the
 *    phase's input mode through the shared ISeam_InputModeArbiter, and pause/unpause.
 *  - Persists a "continue" target slot read through ISeam_SaveSlotManager so a Continue button works.
 *  - Registers itself under FlowTags::Service_AppFlowController so tutorial/AI-director/save-UI read
 *    and drive the phase via the seam without depending on this module.
 *
 * Seams are resolved through the service locator and degrade to documented inert behaviour when a
 * provider is absent (the flow still tracks/announces the phase; only the side effect is skipped).
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_GameFlowSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_AppFlowController
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AppFlowController
	/** The current top-level flow phase tag. */
	virtual FGameplayTag GetActivePhase_Implementation() const override;
	/** Request a transition; returns false if it is not currently allowed. */
	virtual bool RequestTransition_Implementation(FGameplayTag PhaseTag) override;
	/** True if a transition to PhaseTag is currently permitted. */
	virtual bool CanEnterPhase_Implementation(FGameplayTag PhaseTag) const override;
	//~ End ISeam_AppFlowController

	/**
	 * Force a transition to PhaseTag bypassing the allowed-transition check (e.g. a hard error-recovery
	 * jump to Main Menu). Still runs all side effects. Returns false only if PhaseTag is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool ForceTransition(FGameplayTag PhaseTag);

	/** The current active phase tag (BP convenience mirror of the seam getter). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	FGameplayTag GetCurrentPhase() const { return ActivePhase; }

	/** The phase active before the current one (invalid if none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	FGameplayTag GetPreviousPhase() const { return PreviousPhase; }

	/**
	 * The slot name a "Continue" action should resume, computed from ISeam_SaveSlotManager
	 * (most-recent slot) and any explicit SetContinueTarget. Empty if no continue is available.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	FString GetContinueTarget() const;

	/** Explicitly set the continue-target slot (e.g. after the player picks a slot to load). */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void SetContinueTarget(const FString& SlotName);

	/** Convenience: is the flow currently in the Pause phase? */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	bool IsPaused() const;

	// --- Additive deepening (orchestrators + back-stack) ---

	/**
	 * Pop the current phase off the flow back-stack and transition to the phase below it (e.g. Pause ->
	 * InGame, NetError -> MainMenu). Returns false if there is nothing to go back to. Uses ForceTransition
	 * (a back-pop is recovery / overlay-dismiss and must not be gated by guards). Additive.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool GoBack();

	/** The matchmaking controller (owned; drives session flow via ISeam_NetSession). Never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	UFlow_MatchmakingController* GetMatchmaking() const { return Matchmaking; }

	/** The travel coordinator (owned; carry-over save/restore around level travel). Never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	UFlow_TravelCoordinator* GetTravelCoordinator() const { return TravelCoordinator; }

	/** The pause controller (owned; focus-loss auto-pause). Never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	UFlow_PauseController* GetPauseController() const { return PauseController; }

	/** The boot-sequence controller (owned; data-driven boot). Never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	UFlow_BootSequenceController* GetBootController() const { return BootController; }

	/** The flow back-stack / re-entrancy bookkeeping (owned). Never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow")
	UFlow_FlowHistory* GetHistory() const { return History; }

	/**
	 * Public additive accessor exposing the (already-cached) phase definition resolution for the loading
	 * coordinator (which needs a phase's StreamingCategories). Thin wrapper over the private resolver; null
	 * when no definition is authored.
	 */
	const UFlow_FlowStateDefinition* ResolvePhaseDefinitionForLoading(FGameplayTag Phase) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// --- Transition core ---

	/**
	 * Perform the transition to NewPhase (already validated unless bForce). Runs leave side effects for
	 * the old phase, sets ActivePhase, runs enter side effects for the new phase, and broadcasts
	 * DP.Bus.Flow.PhaseChanged. Returns true on success.
	 */
	bool DoTransition(FGameplayTag NewPhase, bool bForce);

	/** Apply the "leave" side effects of OldPhase: pop its screen, pop its input mode, unpause. */
	void ApplyLeaveEffects(const UFlow_FlowStateDefinition* OldDef, FGameplayTag OldPhase);

	/** Apply the "enter" side effects of NewPhase: travel, push screen, push input mode, pause. */
	void ApplyEnterEffects(const UFlow_FlowStateDefinition* NewDef, FGameplayTag NewPhase);

	// --- Side-effect helpers ---

	/** Travel to Def's level (OpenLevel on a host / ClientTravel on a client). No-op if Def has no level. */
	void TravelForPhase(const UFlow_FlowStateDefinition* Def);

	/** Push Def's screen onto the UI mediator (by message bus). No-op if Def has no screen. */
	void PushScreenForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase);

	/** Pop Def's screen from the UI mediator (by message bus). No-op if Def has no screen. */
	void PopScreenForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase);

	/** Push the phase's input mode through the shared arbiter, remembering the request id for pop. */
	void PushInputModeForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase);

	/** Pop the currently-pushed flow input mode (if any) from the shared arbiter. */
	void PopInputMode();

	/** Set engine pause state via the local player controller / UGameplayStatics. */
	void SetGamePaused(bool bPause);

	// --- Resolution helpers ---

	/**
	 * Resolve the UFlow_FlowStateDefinition for Phase: first from the Flow settings PhaseDefinitions
	 * list (loaded), then from the core data registry by DataTag. Null if none authored (the flow then
	 * tracks the phase but applies only defensive default side effects).
	 */
	const UFlow_FlowStateDefinition* ResolvePhaseDefinition(FGameplayTag Phase) const;

	/** Resolve the message bus for this GameInstance, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Resolve the service locator for this GameInstance, or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Resolve a seam interface object registered under Key, or null. */
	UObject* ResolveServiceObject(FGameplayTag Key) const;

	/** Compute the default input-mode tag for a phase when its definition does not specify one. */
	FGameplayTag DefaultInputModeForPhase(FGameplayTag Phase) const;

	/** Read the most-recent slot through ISeam_SaveSlotManager (empty if no provider/slots). */
	FString ReadMostRecentSlot() const;

	/** The local primary player controller (for ClientTravel / pause / input mode owner), or null. */
	APlayerController* GetLocalPlayerController() const;

	/** True if this machine has the authority to drive an absolute level travel (host/standalone). */
	bool HasTravelAuthority() const;

	/**
	 * Consult every registered ISeam_FlowGuard for the From->To edge. Returns true if ALL guards allow it
	 * (or guards are disabled). On a denial, returns false and fills OutDenyReason with the first denier's
	 * reason. Called ONLY from DoTransition's non-force branch (ForceTransition bypasses guards). Additive.
	 */
	bool PassesFlowGuards(FGameplayTag From, FGameplayTag To, FGameplayTag& OutDenyReason) const;

	/** Create + register the built-in profile-loaded guard and the orchestrator subobjects. Additive. */
	void InitializeOrchestrators();

	/** Tear down the orchestrator subobjects + the registered guard. Additive. */
	void ShutdownOrchestrators();

	// --- State ---

	/** The currently-active top-level flow phase. */
	UPROPERTY(Transient)
	FGameplayTag ActivePhase;

	/** The phase active immediately before the current one. */
	UPROPERTY(Transient)
	FGameplayTag PreviousPhase;

	/** Cached, lazily-loaded phase definitions keyed by phase tag (from settings, hard-kept while live). */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UFlow_FlowStateDefinition>> LoadedDefinitions;

	/** Explicit continue-target slot override (empty => derive from ISeam_SaveSlotManager). */
	UPROPERTY(Transient)
	FString ExplicitContinueSlot;

	/** The opaque arbiter request id for the input mode currently pushed by the flow (invalid if none). */
	FGuid ActiveInputModeRequest;

	/** True once we have registered ourselves as the ISeam_AppFlowController provider. */
	bool bRegisteredAsService = false;

	// --- Additive owned orchestrator subobjects (instanced via NewObject(this); GC-kept by UPROPERTY) ---

	/** Matchmaking / session flow driver. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_MatchmakingController> Matchmaking = nullptr;

	/** Carry-over save/restore + travel-failure recovery around level travel. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_TravelCoordinator> TravelCoordinator = nullptr;

	/** Focus-loss / suspend auto-pause controller. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_PauseController> PauseController = nullptr;

	/** Data-driven boot-sequence controller. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_BootSequenceController> BootController = nullptr;

	/** Re-entrancy lock + bounded phase back-stack. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_FlowHistory> History = nullptr;

	/** The built-in profile-loaded flow guard (registered into the locator under Service_FlowGuard). */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_ProfileLoadedGuard> ProfileGuard = nullptr;

	/** True once we have registered the built-in profile guard (so we unregister exactly once). */
	bool bRegisteredProfileGuard = false;
};
