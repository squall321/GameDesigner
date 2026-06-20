// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Dialogue/Narr_DialogueTypes.h"
#include "Narr_DialogueRunnerComponent.generated.h"

class UNarr_DialogueGraph;
struct FNarr_DialogueNode;
struct FNarr_DialogueEdge;
class UNarr_Condition;            // shared (Dialogue/Narr_StoryConditionTypes.h)
class UNarr_Effect;               // shared (Dialogue/Narr_StoryConditionTypes.h)
class INarr_StoryConditionSource; // shared condition/effect facade (story director implements it)
class INarr_DialoguePresenter;
class ISeam_InputModeArbiter;

/**
 * Fired locally when this runner starts a graph. Cosmetic / observer.
 * @param Graph the graph that started.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNarr_OnDialogueStarted, UNarr_DialogueGraph*, Graph);

/**
 * Fired locally when this runner finishes a graph. Cosmetic / observer.
 * @param Graph  the graph that finished.
 * @param Reason why the run ended.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarr_OnDialogueFinished, UNarr_DialogueGraph*, Graph, ENarr_DialogueEndReason, Reason);

/**
 * Per-local-player dialogue runner.
 *
 * Owns the runtime walk of an (immutable) UNarr_DialogueGraph for ONE local player and is the single
 * authority on "what is on screen right now". Its responsibilities:
 *
 *  - WALK the graph node-by-node, presenting Line and Choice nodes through the bound presenter seam
 *    (INarr_DialoguePresenter, set per-runner via SetPresenter — NOT a global locator slot, because each
 *    split-screen / local player owns its own on-screen dialogue UI), silently branching through
 *    Condition nodes, and running Event/entry effects.
 *
 *  - EVALUATE & APPLY through the SHARED narrative condition/effect facade. Node/edge gates are
 *    UNarr_Condition assets and node effects are UNarr_Effect assets (both owned by the story-director
 *    area). The runner resolves the registered INarr_StoryConditionSource (the story director, under
 *    DP.Service.Narrative.ConditionSource) and evaluates UNarr_Condition::IsMet / applies
 *    UNarr_Effect::Apply against it. Authority is enforced BY THE SOURCE: its world-hub write path is a
 *    no-op on clients, so an effect with authoritative writes simply does nothing on a client. A choice
 *    whose target node carries effects is additionally routed through ServerSelectChoice on a client so
 *    the authoritative source on the server applies them.
 *
 *  - BROADCAST observer-only bus events (DP.Bus.Narrative.*) as it progresses. These NEVER drive flow;
 *    they are notifications for UI fx / audio / analytics. The presenter seam is the ONLY flow driver.
 *
 *  - PUSH a dialogue input mode onto the shared ISeam_InputModeArbiter while a conversation is active so
 *    gameplay input is suppressed, and pop it when the conversation ends.
 *
 * The component holds only LOCAL run state and is therefore NOT replicated; the only wire traffic is the
 * client->server choice-intent RPC. The graph is shared immutable content; all mutable state lives here.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent), Blueprintable)
class DESIGNPATTERNSNARRATIVE_API UNarr_DialogueRunnerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNarr_DialogueRunnerComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Presenter binding (per-runner, local) --------------------------------------------------

	/**
	 * Bind the local presenter this runner drives. The presenter is held as a TScriptInterface so a UMG
	 * widget (BP) or a C++ HUD view-model can implement it. Binding while a conversation is on screen
	 * re-presents the current node on the new presenter (and hides the old one).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Runner")
	void SetPresenter(const TScriptInterface<INarr_DialoguePresenter>& InPresenter);

	/** @return the currently-bound presenter (may be invalid). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Runner")
	TScriptInterface<INarr_DialoguePresenter> GetPresenter() const;

	// ---- Run control ----------------------------------------------------------------------------

	/**
	 * Begin running Graph from its start node on this local machine. If a conversation is already active
	 * it is aborted first. No-op (logged) when Graph is null or has no valid start node.
	 * @return true if a run started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Runner")
	bool StartDialogue(UNarr_DialogueGraph* Graph);

	/**
	 * Advance the current Line node (the player "pressed continue"). No-op if the active node is not an
	 * advanceable line or no conversation is active. For a Choice node use SelectChoice instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Runner")
	void AdvanceLine();

	/**
	 * Commit a choice by its ChoiceId on the active Choice node. If the matching edge's target node has
	 * effects and this machine is a client, the selection is routed to the server via ServerSelectChoice
	 * and the authoritative effects are applied there; the local cosmetic flow advances regardless. No-op
	 * for an unknown / disabled choice id.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Runner")
	void SelectChoice(FGameplayTag ChoiceId);

	/** Stop the active conversation (hides the presenter, pops input mode) with reason Aborted. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Runner")
	void StopDialogue();

	/** @return true if a conversation is currently running on this runner. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Runner")
	bool IsRunning() const { return ActiveGraph != nullptr; }

	/** @return the graph currently running, or null. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Runner")
	UNarr_DialogueGraph* GetActiveGraph() const { return ActiveGraph; }

	/** @return the id of the node currently presented, or an invalid tag when idle. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Runner")
	FGameplayTag GetActiveNodeId() const { return ActiveNodeId; }

	// ---- Events ---------------------------------------------------------------------------------

	/** Fired locally when a graph starts on this runner. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Runner")
	FNarr_OnDialogueStarted OnDialogueStarted;

	/** Fired locally when a graph ends on this runner. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Runner")
	FNarr_OnDialogueFinished OnDialogueFinished;

	// ---- Tunables (no hardcoded magic gameplay numbers) -----------------------------------------

	/**
	 * Input-mode priority pushed onto the arbiter while dialogue is active. Higher beats lower; tune so a
	 * dialogue lock sits above gameplay but below a higher-priority modal (e.g. a pause menu).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Runner")
	int32 InputModePriority = 100;

	/**
	 * When true, the runner pushes/pops the dialogue input mode on ISeam_InputModeArbiter around an
	 * active conversation. Disable for fully diegetic dialogue that should not lock gameplay input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Runner")
	bool bManageInputMode = true;

	/**
	 * When true, the runner re-publishes its progress as observer-only events on the message bus
	 * (DP.Bus.Narrative.*). These are notifications only and never drive flow.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Runner")
	bool bBroadcastObserverEvents = true;

private:
	// ---- Active run state (LOCAL, not replicated) -----------------------------------------------

	/** The graph currently running, or null when idle. Not owned (shared immutable content). */
	UPROPERTY(Transient)
	TObjectPtr<UNarr_DialogueGraph> ActiveGraph = nullptr;

	/** The presenter this runner drives. Held as a script interface so BP or C++ can implement it. */
	UPROPERTY(Transient)
	TScriptInterface<INarr_DialoguePresenter> Presenter;

	/** Id of the node currently presented (line/choice), or invalid when idle. */
	FGameplayTag ActiveNodeId;

	/** Timer handle backing a Line node's AutoAdvanceSeconds. Cleared whenever the node changes. */
	FTimerHandle AutoAdvanceTimer;

	// ---- Cached seams (resolved lazily, held weakly) --------------------------------------------

	/**
	 * Shared condition/effect facade (the story director). Resolved from the locator under
	 * DP.Service.Narrative.ConditionSource. Conditions evaluate and effects apply against it; its
	 * world-hub write path is authority-guarded, so client-side effect application is a safe no-op.
	 */
	mutable TWeakInterfacePtr<INarr_StoryConditionSource> ConditionSource;

	/** Input-mode arbiter seam, resolved from the locator. Non-owning. */
	TWeakInterfacePtr<ISeam_InputModeArbiter> InputArbiter;

	/** The request id returned by the arbiter's PushInputMode, popped when dialogue ends. */
	FGuid InputModeRequest;

	// ---- Flow internals -------------------------------------------------------------------------

	/**
	 * Enter the node with NodeId: run its entry effects (through the source), then present it / branch. A
	 * node whose EntryCondition fails is treated as unreachable and ends the run as a dead end.
	 */
	void EnterNode(const FGameplayTag& NodeId);

	/** Present a Line node and arm auto-advance if configured. */
	void PresentLineNode(const FNarr_DialogueNode& Node);

	/** Present a Choice node's eligible/enabled choices through the presenter. */
	void PresentChoiceNode(const FNarr_DialogueNode& Node);

	/**
	 * Follow the first satisfiable outgoing edge of a branch node (Line/Condition/Event). @return true
	 * and fills OutTarget with the chosen edge's target node id; OutTarget may be an invalid tag (a
	 * deliberate terminator). @return false when no edge is eligible at all.
	 */
	bool ChooseBranchTarget(const FNarr_DialogueNode& Node, FGameplayTag& OutTarget) const;

	/** Run a node's entry effects through the condition source (writes self-no-op on clients). */
	void ApplyNodeEntryEffects(const FNarr_DialogueNode& Node);

	/** Finish the run with Reason: hide the presenter, pop input mode, fire events, clear state. */
	void FinishRun(ENarr_DialogueEndReason Reason);

	/** True if the committed choice's target node carries any entry effect (so authority is needed). */
	bool ChoiceTargetHasEffects(const FNarr_DialogueNode& Node, const FNarr_DialogueEdge* CommittedEdge) const;

	/** Apply the local cosmetic advance after a choice (move to the edge's target). */
	void CommitChoiceLocally(FGameplayTag ChoiceId);

	/** Re-present whatever node is currently active onto the bound presenter (used on rebind). */
	void RepresentActiveNode();

	/** Evaluate a (possibly null) condition through the resolved source. Null condition = true. */
	bool EvaluateGate(const UNarr_Condition* Condition) const;

	/** Resolve (lazily) the shared condition/effect source from the service locator. May be null. */
	TScriptInterface<INarr_StoryConditionSource> ResolveConditionSource() const;

	/** Resolve the input-mode arbiter seam from the service locator. @return the raw interface or null. */
	ISeam_InputModeArbiter* ResolveInputArbiter();

	/** Push the dialogue input mode (if managed and not already pushed). */
	void PushDialogueInputMode();

	/** Pop the dialogue input mode (if pushed). */
	void PopDialogueInputMode();

	/** Broadcast one observer-only narrative event on the bus (no-op if disabled). */
	void BroadcastObserver(const FGameplayTag& Channel, const FNarr_DialogueBusEvent& Event) const;

	/** Timer callback for an auto-advancing line. */
	void HandleAutoAdvance();

	/** True if this machine has authority for the owning actor (server / standalone / listen host). */
	bool HasOwnerAuthority() const;

	// ---- Client -> server choice intent ---------------------------------------------------------

	/**
	 * Client -> server intent to commit a choice whose target node carries effects. The server validates
	 * the request (graph resolves, node is a choice, choice exists and its guard passes under the server's
	 * evaluation) and applies the target node's effects through the AUTHORITATIVE condition source. The
	 * client meanwhile advances its own cosmetic flow.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSelectChoice(FGameplayTag GraphTag, FGameplayTag NodeId, FGameplayTag ChoiceId);

	/** Server-side: apply the authoritative entry effects of a committed choice's target node. */
	void ApplyChoiceEffectsAuthoritative(UNarr_DialogueGraph* Graph, const FGameplayTag& NodeId, const FGameplayTag& ChoiceId);
};
