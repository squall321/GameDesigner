// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Dialogue/Narr_DialogueRunnerComponent.h"
#include "Dialogue/Narr_DialogueGraph.h"
#include "Logic/Narr_Condition.h"                 // shared UNarr_Condition
#include "Logic/Narr_Effect.h"                    // shared UNarr_Effect
#include "Seam/Narr_StoryConditionSource.h"       // INarr_StoryConditionSource
#include "Seam/Narr_DialoguePresenter.h"
#include "DesignPatternsNarrativeModule.h"
#include "Story/Narr_StoryNativeTags.h"          // DP.Service.Narrative.ConditionSource

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"

// Seams (public deps).
#include "Input/Seam_InputModeArbiter.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UNarr_DialogueRunnerComponent::UNarr_DialogueRunnerComponent()
{
	// The runner does not tick; it is driven by presenter callbacks and timers.
	PrimaryComponentTick.bCanEverTick = false;

	// Local-presentation state is not replicated; only the choice-intent RPC crosses the wire. The
	// component must still be registered as replicated so its Server RPC is routed.
	SetIsReplicatedByDefault(true);
}

void UNarr_DialogueRunnerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the input arbiter eagerly so a missing Platform module is logged once at startup rather
	// than on the first conversation. The condition source is resolved lazily (the story director may
	// register after this component begins play).
	ResolveInputArbiter();
}

void UNarr_DialogueRunnerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Tear down any active conversation so the presenter and input mode are released on teardown.
	if (IsRunning())
	{
		FinishRun(ENarr_DialogueEndReason::Aborted);
	}
	else
	{
		PopDialogueInputMode();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoAdvanceTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void UNarr_DialogueRunnerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Intentionally no replicated properties: dialogue presentation is local/cosmetic and is driven by
	// already-replicated gameplay (the world hub, read via the condition source) plus the per-machine
	// graph walk. Only the client->server choice-intent RPC crosses the wire.
}

// --- Presenter binding ---------------------------------------------------------------------------

void UNarr_DialogueRunnerComponent::SetPresenter(const TScriptInterface<INarr_DialoguePresenter>& InPresenter)
{
	if (Presenter.GetObject() == InPresenter.GetObject())
	{
		return;
	}

	// Hide on the old presenter before swapping.
	if (Presenter.GetObject() && Presenter.GetObject()->Implements<UNarr_DialoguePresenter>())
	{
		INarr_DialoguePresenter::Execute_HideDialogue(Presenter.GetObject());
	}

	Presenter = InPresenter;

	// If a conversation is live, re-present the current node on the new presenter so the swap is seamless.
	if (IsRunning())
	{
		RepresentActiveNode();
	}
}

TScriptInterface<INarr_DialoguePresenter> UNarr_DialogueRunnerComponent::GetPresenter() const
{
	return Presenter;
}

// --- Run control ---------------------------------------------------------------------------------

bool UNarr_DialogueRunnerComponent::StartDialogue(UNarr_DialogueGraph* Graph)
{
	if (!Graph)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::StartDialogue: null graph."));
		return false;
	}

	const FNarr_DialogueNode* Start = Graph->GetStartNode();
	if (!Start)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::StartDialogue: graph '%s' has no valid start node."),
			*Graph->DataTag.ToString());
		return false;
	}

	// Abort any conversation already in progress before starting a new one.
	if (IsRunning())
	{
		FinishRun(ENarr_DialogueEndReason::Aborted);
	}

	ActiveGraph = Graph;
	ActiveNodeId = FGameplayTag();

	PushDialogueInputMode();

	OnDialogueStarted.Broadcast(Graph);
	BroadcastObserver(NarrativeNativeTags::Bus_Narrative_DialogueStarted,
		FNarr_DialogueBusEvent(Graph->DataTag, Start->NodeId, Start->Speaker, 0));

	UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::StartDialogue: '%s' from node '%s'."),
		*Graph->DataTag.ToString(), *Start->NodeId.ToString());

	EnterNode(Start->NodeId);
	return IsRunning();
}

void UNarr_DialogueRunnerComponent::AdvanceLine()
{
	if (!IsRunning())
	{
		return;
	}

	const FNarr_DialogueNode* Node = ActiveGraph->FindNode(ActiveNodeId);
	if (!Node || Node->Kind != ENarr_NodeKind::Line)
	{
		// Only line nodes are advanceable; a choice node requires SelectChoice.
		return;
	}

	// Cancel any pending auto-advance for this line (the player advanced manually).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoAdvanceTimer);
	}

	FGameplayTag Target;
	if (ChooseBranchTarget(*Node, Target) && Target.IsValid())
	{
		EnterNode(Target);
	}
	else
	{
		// No eligible outgoing edge: a natural end of this branch.
		FinishRun(ENarr_DialogueEndReason::Completed);
	}
}

void UNarr_DialogueRunnerComponent::SelectChoice(FGameplayTag ChoiceId)
{
	if (!IsRunning())
	{
		return;
	}

	const FNarr_DialogueNode* Node = ActiveGraph->FindNode(ActiveNodeId);
	if (!Node || Node->Kind != ENarr_NodeKind::Choice)
	{
		return;
	}

	// Locate the chosen edge and confirm it is enabled (its guard passes) before committing.
	const FNarr_DialogueEdge* Chosen = Node->Edges.FindByPredicate(
		[&ChoiceId](const FNarr_DialogueEdge& E) { return E.IsChoice() && E.ChoiceId == ChoiceId; });

	if (!Chosen)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::SelectChoice: unknown choice '%s' on node '%s'."),
			*ChoiceId.ToString(), *ActiveNodeId.ToString());
		return;
	}

	if (!EvaluateGate(Chosen->Guard))
	{
		UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::SelectChoice: choice '%s' is disabled; ignoring."), *ChoiceId.ToString());
		return;
	}

	BroadcastObserver(NarrativeNativeTags::Bus_Narrative_ChoiceSelected,
		FNarr_DialogueBusEvent(ActiveGraph->DataTag, ActiveNodeId, ChoiceId, 0));

	// If the committed choice's target node carries effects and this machine is a client, route the
	// selection to the server so the authoritative source applies those effects there. Local cosmetic
	// flow still advances below for immediate feedback (client-side effect application is a safe no-op).
	if (ChoiceTargetHasEffects(*Node, Chosen) && !HasOwnerAuthority())
	{
		ServerSelectChoice(ActiveGraph->DataTag, ActiveNodeId, ChoiceId);
	}

	CommitChoiceLocally(ChoiceId);
}

void UNarr_DialogueRunnerComponent::StopDialogue()
{
	if (IsRunning())
	{
		FinishRun(ENarr_DialogueEndReason::Aborted);
	}
}

// --- Flow internals ------------------------------------------------------------------------------

void UNarr_DialogueRunnerComponent::EnterNode(const FGameplayTag& NodeId)
{
	if (!IsRunning())
	{
		return;
	}

	const FNarr_DialogueNode* Node = ActiveGraph->FindNode(NodeId);
	if (!Node)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::EnterNode: node '%s' not found; ending as dead end."), *NodeId.ToString());
		FinishRun(ENarr_DialogueEndReason::DeadEnd);
		return;
	}

	// Reachability gate: a node whose entry condition fails is unreachable.
	if (Node->EntryCondition && !EvaluateGate(Node->EntryCondition))
	{
		UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::EnterNode: node '%s' entry condition failed; dead end."), *NodeId.ToString());
		FinishRun(ENarr_DialogueEndReason::DeadEnd);
		return;
	}

	ActiveNodeId = NodeId;

	// Run entry effects through the source (its world-hub write path no-ops on clients).
	ApplyNodeEntryEffects(*Node);

	switch (Node->Kind)
	{
	case ENarr_NodeKind::Line:
		PresentLineNode(*Node);
		break;

	case ENarr_NodeKind::Choice:
		PresentChoiceNode(*Node);
		break;

	case ENarr_NodeKind::Condition:
	case ENarr_NodeKind::Event:
	{
		// Silent nodes: immediately follow the first satisfiable edge (Event already ran its effects).
		FGameplayTag Target;
		if (ChooseBranchTarget(*Node, Target) && Target.IsValid())
		{
			EnterNode(Target);
		}
		else
		{
			FinishRun(ENarr_DialogueEndReason::Completed);
		}
		break;
	}

	default:
		FinishRun(ENarr_DialogueEndReason::DeadEnd);
		break;
	}
}

void UNarr_DialogueRunnerComponent::PresentLineNode(const FNarr_DialogueNode& Node)
{
	const FNarr_DialogueLine Line = Node.MakeLine();

	if (Presenter.GetObject() && Presenter.GetObject()->Implements<UNarr_DialoguePresenter>())
	{
		INarr_DialoguePresenter::Execute_ShowLine(Presenter.GetObject(), Line);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::PresentLineNode: no presenter bound; line '%s' not shown."),
			*Node.NodeId.ToString());
	}

	BroadcastObserver(NarrativeNativeTags::Bus_Narrative_LineShown,
		FNarr_DialogueBusEvent(ActiveGraph->DataTag, Node.NodeId, Node.Speaker, 0));

	// Arm auto-advance if configured; otherwise the runner waits for AdvanceLine().
	if (Line.bAutoAdvances())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(AutoAdvanceTimer, this,
				&UNarr_DialogueRunnerComponent::HandleAutoAdvance, Line.AutoAdvanceSeconds, /*bLoop*/ false);
		}
	}
}

void UNarr_DialogueRunnerComponent::PresentChoiceNode(const FNarr_DialogueNode& Node)
{
	TArray<FNarr_DialogueChoice> Choices;
	Choices.Reserve(Node.Edges.Num());

	for (const FNarr_DialogueEdge& Edge : Node.Edges)
	{
		if (!Edge.IsChoice())
		{
			continue;
		}

		// Pre-evaluate the gate and bake it into bEnabled so the presenter never runs the mini-language.
		const bool bEnabled = EvaluateGate(Edge.Guard);
		Choices.Emplace(Edge.ChoiceText, Edge.ChoiceId, bEnabled);
	}

	if (Presenter.GetObject() && Presenter.GetObject()->Implements<UNarr_DialoguePresenter>())
	{
		INarr_DialoguePresenter::Execute_ShowChoices(Presenter.GetObject(), Choices);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::PresentChoiceNode: no presenter bound; choices for '%s' not shown."),
			*Node.NodeId.ToString());
	}

	BroadcastObserver(NarrativeNativeTags::Bus_Narrative_ChoicesShown,
		FNarr_DialogueBusEvent(ActiveGraph->DataTag, Node.NodeId, FGameplayTag(), Choices.Num()));
}

bool UNarr_DialogueRunnerComponent::ChooseBranchTarget(const FNarr_DialogueNode& Node, FGameplayTag& OutTarget) const
{
	for (const FNarr_DialogueEdge& Edge : Node.Edges)
	{
		// On a branch node, choice-carrying edges are not auto-followed (only Choice nodes consume them).
		if (Node.Kind == ENarr_NodeKind::Choice && Edge.IsChoice())
		{
			continue;
		}

		// First edge whose guard passes (a null guard always passes).
		if (EvaluateGate(Edge.Guard))
		{
			OutTarget = Edge.TargetNodeId;
			return true;
		}
	}

	OutTarget = FGameplayTag();
	return false;
}

void UNarr_DialogueRunnerComponent::ApplyNodeEntryEffects(const FNarr_DialogueNode& Node)
{
	if (Node.EntryEffects.Num() == 0)
	{
		return;
	}

	const TScriptInterface<INarr_StoryConditionSource> Source = ResolveConditionSource();
	if (!Source)
	{
		UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::ApplyNodeEntryEffects: no condition source; effects on '%s' skipped."),
			*Node.NodeId.ToString());
		return;
	}

	for (const TObjectPtr<UNarr_Effect>& Effect : Node.EntryEffects)
	{
		if (Effect)
		{
			// The source's write path is authority-guarded, so this is a safe no-op on clients.
			Effect->Apply(Source);
		}
	}
}

void UNarr_DialogueRunnerComponent::CommitChoiceLocally(FGameplayTag ChoiceId)
{
	const FNarr_DialogueNode* Node = ActiveGraph ? ActiveGraph->FindNode(ActiveNodeId) : nullptr;
	if (!Node)
	{
		FinishRun(ENarr_DialogueEndReason::DeadEnd);
		return;
	}

	const FNarr_DialogueEdge* Chosen = Node->Edges.FindByPredicate(
		[&ChoiceId](const FNarr_DialogueEdge& E) { return E.IsChoice() && E.ChoiceId == ChoiceId; });

	if (!Chosen)
	{
		FinishRun(ENarr_DialogueEndReason::DeadEnd);
		return;
	}

	if (Chosen->TargetNodeId.IsValid())
	{
		EnterNode(Chosen->TargetNodeId);
	}
	else
	{
		// A choice edge with no target is a deliberate end-of-branch.
		FinishRun(ENarr_DialogueEndReason::Completed);
	}
}

void UNarr_DialogueRunnerComponent::RepresentActiveNode()
{
	if (!IsRunning())
	{
		return;
	}

	const FNarr_DialogueNode* Node = ActiveGraph->FindNode(ActiveNodeId);
	if (!Node)
	{
		return;
	}

	// Re-present without re-running entry effects (those already ran on first enter).
	if (Node->Kind == ENarr_NodeKind::Line)
	{
		if (Presenter.GetObject() && Presenter.GetObject()->Implements<UNarr_DialoguePresenter>())
		{
			INarr_DialoguePresenter::Execute_ShowLine(Presenter.GetObject(), Node->MakeLine());
		}
	}
	else if (Node->Kind == ENarr_NodeKind::Choice)
	{
		PresentChoiceNode(*Node);
	}
}

void UNarr_DialogueRunnerComponent::FinishRun(ENarr_DialogueEndReason Reason)
{
	UNarr_DialogueGraph* FinishedGraph = ActiveGraph;
	const FGameplayTag GraphTag = FinishedGraph ? FinishedGraph->DataTag : FGameplayTag();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoAdvanceTimer);
	}

	// Hide the presenter UI.
	if (Presenter.GetObject() && Presenter.GetObject()->Implements<UNarr_DialoguePresenter>())
	{
		INarr_DialoguePresenter::Execute_HideDialogue(Presenter.GetObject());
	}

	PopDialogueInputMode();

	// Clear run state BEFORE firing observers so a listener that immediately starts a new conversation
	// sees a clean idle runner.
	ActiveGraph = nullptr;
	ActiveNodeId = FGameplayTag();

	BroadcastObserver(NarrativeNativeTags::Bus_Narrative_DialogueFinished,
		FNarr_DialogueBusEvent(GraphTag, FGameplayTag(), FGameplayTag(), static_cast<int32>(Reason)));

	OnDialogueFinished.Broadcast(FinishedGraph, Reason);

	UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::FinishRun: graph '%s' ended (%d)."),
		*GraphTag.ToString(), static_cast<int32>(Reason));
}

bool UNarr_DialogueRunnerComponent::ChoiceTargetHasEffects(const FNarr_DialogueNode& Node, const FNarr_DialogueEdge* CommittedEdge) const
{
	if (!ActiveGraph || !CommittedEdge || !CommittedEdge->TargetNodeId.IsValid())
	{
		return false;
	}

	if (const FNarr_DialogueNode* Target = ActiveGraph->FindNode(CommittedEdge->TargetNodeId))
	{
		for (const TObjectPtr<UNarr_Effect>& Effect : Target->EntryEffects)
		{
			if (Effect)
			{
				return true;
			}
		}
	}
	return false;
}

void UNarr_DialogueRunnerComponent::HandleAutoAdvance()
{
	// The timer fired for the active line: advance exactly as a manual continue would.
	AdvanceLine();
}

// --- Condition evaluation ------------------------------------------------------------------------

bool UNarr_DialogueRunnerComponent::EvaluateGate(const UNarr_Condition* Condition) const
{
	// A null condition is an absent gate -> true (never blocks).
	if (!Condition)
	{
		return true;
	}

	const TScriptInterface<INarr_StoryConditionSource> Source = ResolveConditionSource();
	// With no source, the condition evaluates against an invalid TScriptInterface; the shared conditions
	// fail closed to their bDefaultWhenNoSource (conservatively false), which is the desired safe gating.
	return Condition->IsMet(Source);
}

// --- Seam resolution -----------------------------------------------------------------------------

TScriptInterface<INarr_StoryConditionSource> UNarr_DialogueRunnerComponent::ResolveConditionSource() const
{
	TScriptInterface<INarr_StoryConditionSource> Result;

	if (UObject* CachedObj = ConditionSource.GetObject())
	{
		Result.SetObject(CachedObj);
		Result.SetInterface(ConditionSource.Get());
		return Result;
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Result;
	}

	// The story director registers itself as the condition/effect source under this key.
	UObject* Provider = Locator->ResolveService(NarrativeStoryNativeTags::Service_Narrative_ConditionSource);
	if (Provider && Provider->Implements<UNarr_StoryConditionSource>())
	{
		INarr_StoryConditionSource* Raw = Cast<INarr_StoryConditionSource>(Provider);
		ConditionSource = Raw;
		Result.SetObject(Provider);
		Result.SetInterface(Raw);
	}
	return Result;
}

ISeam_InputModeArbiter* UNarr_DialogueRunnerComponent::ResolveInputArbiter()
{
	if (ISeam_InputModeArbiter* Cached = InputArbiter.Get())
	{
		return Cached;
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}

	// The Platform module's input router registers under DP.Service.InputModeArbiter.
	static const FGameplayTag ArbiterServiceTag =
		FGameplayTag::RequestGameplayTag(TEXT("DP.Service.InputModeArbiter"), /*bErrorIfNotFound*/ false);
	if (!ArbiterServiceTag.IsValid())
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(ArbiterServiceTag);
	if (Provider && Provider->Implements<USeam_InputModeArbiter>())
	{
		ISeam_InputModeArbiter* Raw = Cast<ISeam_InputModeArbiter>(Provider);
		InputArbiter = Raw;
		return Raw;
	}
	return nullptr;
}

// --- Input mode ----------------------------------------------------------------------------------

void UNarr_DialogueRunnerComponent::PushDialogueInputMode()
{
	if (!bManageInputMode || InputModeRequest.IsValid())
	{
		return;
	}

	ISeam_InputModeArbiter* Arbiter = ResolveInputArbiter();
	if (!Arbiter)
	{
		// Not fatal: dialogue still presents, it just does not lock gameplay input.
		UE_LOG(LogDP, Verbose, TEXT("Narr_Runner::PushDialogueInputMode: no input arbiter; dialogue will not lock input."));
		return;
	}

	if (UObject* ArbiterObj = InputArbiter.GetObject())
	{
		InputModeRequest = ISeam_InputModeArbiter::Execute_PushInputMode(
			ArbiterObj, NarrativeNativeTags::InputMode_Dialogue, InputModePriority);
	}
}

void UNarr_DialogueRunnerComponent::PopDialogueInputMode()
{
	if (!InputModeRequest.IsValid())
	{
		return;
	}

	if (UObject* ArbiterObj = InputArbiter.GetObject())
	{
		if (ArbiterObj->Implements<USeam_InputModeArbiter>())
		{
			ISeam_InputModeArbiter::Execute_PopInputMode(ArbiterObj, InputModeRequest);
		}
	}
	InputModeRequest.Invalidate();
}

// --- Observer bus events -------------------------------------------------------------------------

void UNarr_DialogueRunnerComponent::BroadcastObserver(const FGameplayTag& Channel, const FNarr_DialogueBusEvent& Event) const
{
	if (!bBroadcastObserverEvents || !Channel.IsValid())
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FInstancedStruct Payload;
	Payload.InitializeAs<FNarr_DialogueBusEvent>(Event);
	Bus->BroadcastPayload(Channel, Payload, const_cast<UNarr_DialogueRunnerComponent*>(this));
}

// --- Authority helpers ---------------------------------------------------------------------------

bool UNarr_DialogueRunnerComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// --- Client -> server choice intent --------------------------------------------------------------

bool UNarr_DialogueRunnerComponent::ServerSelectChoice_Validate(FGameplayTag /*GraphTag*/, FGameplayTag /*NodeId*/, FGameplayTag ChoiceId)
{
	// Reject obviously malformed intents; the heavy validation (graph/node/choice existence + guard) is
	// done in the implementation against the server's own authoritative content, never trusting the
	// client's claimed ids beyond their shape.
	return ChoiceId.IsValid();
}

void UNarr_DialogueRunnerComponent::ServerSelectChoice_Implementation(FGameplayTag GraphTag, FGameplayTag NodeId, FGameplayTag ChoiceId)
{
	// Resolve the graph authoritatively. On a listen-server host the server's own runner has ActiveGraph
	// set; on a dedicated server the player's runner only ran cosmetically on the client, so resolve the
	// graph by tag from the data registry. Either way we re-derive node/choice/guard from immutable
	// content and never trust the client's flow position.
	UNarr_DialogueGraph* Graph = ActiveGraph;
	if (!Graph || Graph->DataTag != GraphTag)
	{
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			Graph = Registry->Find<UNarr_DialogueGraph>(GraphTag);
		}
	}

	if (!Graph)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::ServerSelectChoice: could not resolve graph '%s'; ignoring."),
			*GraphTag.ToString());
		return;
	}

	const FNarr_DialogueNode* Node = Graph->FindNode(NodeId);
	if (!Node || Node->Kind != ENarr_NodeKind::Choice)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::ServerSelectChoice: node '%s' missing or not a choice."), *NodeId.ToString());
		return;
	}

	const FNarr_DialogueEdge* Chosen = Node->Edges.FindByPredicate(
		[&ChoiceId](const FNarr_DialogueEdge& E) { return E.IsChoice() && E.ChoiceId == ChoiceId; });
	if (!Chosen)
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::ServerSelectChoice: choice '%s' not found on node '%s'."),
			*ChoiceId.ToString(), *NodeId.ToString());
		return;
	}

	// The choice must be enabled under the SERVER's evaluation of its guard (authoritative re-check).
	if (!EvaluateGate(Chosen->Guard))
	{
		UE_LOG(LogDP, Warning, TEXT("Narr_Runner::ServerSelectChoice: choice '%s' guard failed on server; rejecting."),
			*ChoiceId.ToString());
		return;
	}

	ApplyChoiceEffectsAuthoritative(Graph, NodeId, ChoiceId);
}

void UNarr_DialogueRunnerComponent::ApplyChoiceEffectsAuthoritative(UNarr_DialogueGraph* Graph, const FGameplayTag& NodeId, const FGameplayTag& ChoiceId)
{
	if (!Graph)
	{
		return;
	}

	const FNarr_DialogueNode* Node = Graph->FindNode(NodeId);
	if (!Node)
	{
		return;
	}

	const FNarr_DialogueEdge* Chosen = Node->Edges.FindByPredicate(
		[&ChoiceId](const FNarr_DialogueEdge& E) { return E.IsChoice() && E.ChoiceId == ChoiceId; });
	if (!Chosen || !Chosen->TargetNodeId.IsValid())
	{
		return;
	}

	// Run the target node's entry effects through the AUTHORITATIVE source (already on the server). The
	// source's hub write path takes hold here and replicates to clients via the hub's carrier.
	if (const FNarr_DialogueNode* Target = Graph->FindNode(Chosen->TargetNodeId))
	{
		ApplyNodeEntryEffects(*Target);
	}
}
