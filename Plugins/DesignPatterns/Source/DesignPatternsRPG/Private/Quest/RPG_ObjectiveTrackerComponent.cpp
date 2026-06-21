// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_ObjectiveTrackerComponent.h"
#include "Quest/RPG_QuestGraphDefinition.h"
#include "Quest/RPG_QuestGraphTypes.h"
#include "Quest/RPG_QuestLogComponent.h"
#include "Quest/RPG_QuestBusEvent.h"
#include "Quest/RPG_QuestNativeTags.h"
#include "Quest/Objectives/RPG_Objective.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"
#include "Data/DPDataRegistrySubsystem.h"

// World hub (PRIVATE dependency — resolved only in .cpp, never exposed in an RPG public header).
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"

// Reputation + identity seams (PUBLIC Seams dep).
#include "Reputation/Seam_Reputation.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Identity/Seam_EntityId.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// ===== Fast-array item callbacks (clients) =======================================================

void FRPG_QuestStageState::PreReplicatedRemove(const FRPG_QuestStageStateArray& InArraySerializer)
{
	// Nothing to surface on removal beyond a generic refresh; the tracker re-evaluates visible state.
	if (URPG_ObjectiveTrackerComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->HandleStageReplicated(QuestTag, FGameplayTag());
	}
}

void FRPG_QuestStageState::PostReplicatedAdd(const FRPG_QuestStageStateArray& InArraySerializer)
{
	if (URPG_ObjectiveTrackerComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->HandleStageReplicated(QuestTag, CurrentStage);
	}
}

void FRPG_QuestStageState::PostReplicatedChange(const FRPG_QuestStageStateArray& InArraySerializer)
{
	if (URPG_ObjectiveTrackerComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->HandleStageReplicated(QuestTag, CurrentStage);
	}
}

// ===== Component lifecycle =======================================================================

URPG_ObjectiveTrackerComponent::URPG_ObjectiveTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // enabled only while a time-limited stage is active
	SetIsReplicatedByDefault(true);

	// Wire the fast-array's non-replicated back-pointer (server + client) so item callbacks reach us.
	StageStates.OwnerComponent = this;
}

void URPG_ObjectiveTrackerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_ObjectiveTrackerComponent, StageStates);
}

void URPG_ObjectiveTrackerComponent::BeginPlay()
{
	Super::BeginPlay();
	StageStates.OwnerComponent = this;
}

void URPG_ObjectiveTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DeactivateObjectives();
	Super::EndPlay(EndPlayReason);
}

void URPG_ObjectiveTrackerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (HasAuthoritySafe() && bAnyTimeLimit)
	{
		TickTimeLimits();
	}
}

bool URPG_ObjectiveTrackerComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// ===== Resolution helpers ========================================================================

URPG_QuestLogComponent* URPG_ObjectiveTrackerComponent::GetQuestLog() const
{
	if (QuestLog)
	{
		return QuestLog;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<URPG_QuestLogComponent>();
	}
	return nullptr;
}

URPG_QuestGraphDefinition* URPG_ObjectiveTrackerComponent::ResolveGraph(const FGameplayTag& QuestTag) const
{
	if (!QuestTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		// Quests share the "RPG_Quest" bucket; the branching subclass is found by the same tag.
		return Cast<URPG_QuestGraphDefinition>(Registry->FindByTag(QuestTag));
	}
	return nullptr;
}

int32 URPG_ObjectiveTrackerComponent::FindStateIndex(const FGameplayTag& QuestTag) const
{
	for (int32 i = 0; i < StageStates.Entries.Num(); ++i)
	{
		if (StageStates.Entries[i].QuestTag == QuestTag)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FGuid URPG_ObjectiveTrackerComponent::ResolveOwnerEntityGuid() const
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->Implements<USeam_EntityIdentity>())
	{
		const FSeam_EntityId Id = ISeam_EntityIdentity::Execute_GetEntityId(const_cast<AActor*>(Owner));
		return Id.Value;
	}
	return FGuid();
}

// ===== Public API ================================================================================

bool URPG_ObjectiveTrackerComponent::ActivateQuestGraph(FGameplayTag QuestTag)
{
	if (!HasAuthoritySafe())
	{
		return false;
	}
	URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	if (!Graph || !Graph->IsBranching())
	{
		UE_LOG(LogDP, Verbose, TEXT("[RPG] ActivateQuestGraph: '%s' is not a branching quest; left to the base log."),
			*QuestTag.ToString());
		return false;
	}

	// Accept gates.
	for (const FRPG_StageGate& Gate : Graph->AcceptGates)
	{
		if (!EvaluateGate(QuestTag, Gate))
		{
			UE_LOG(LogDP, Verbose, TEXT("[RPG] Quest '%s' accept gate failed."), *QuestTag.ToString());
			return false;
		}
	}

	const FRPG_QuestStage* Start = Graph->GetStartStage();
	if (!Start)
	{
		UE_LOG(LogDP, Warning, TEXT("[RPG] Quest '%s' has no valid StartStage."), *QuestTag.ToString());
		return false;
	}

	// Start the quest on the real log (authority-guarded inside).
	if (URPG_QuestLogComponent* Log = GetQuestLog())
	{
		Log->StartQuest(QuestTag);
	}

	// Create/refresh the stage-state entry.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	int32 Index = FindStateIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		FRPG_QuestStageState NewState;
		NewState.QuestTag = QuestTag;
		NewState.QuestStartWorldTime = Now;
		Index = StageStates.Entries.Add(NewState);
	}
	else
	{
		StageStates.Entries[Index].QuestStartWorldTime = Now;
	}

	BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_Activated, QuestTag, Graph->StartStage, 0);

	EnterStage(QuestTag, Graph->StartStage);
	return true;
}

void URPG_ObjectiveTrackerComponent::ReportProgress(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 Delta)
{
	if (!HasAuthoritySafe() || Delta == 0)
	{
		return;
	}
	URPG_QuestLogComponent* Log = GetQuestLog();
	if (!Log)
	{
		return;
	}

	const int32 NewCount = Log->AdvanceObjective(QuestTag, ObjectiveTag, Delta);
	BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_ObjectiveProgress, QuestTag, ObjectiveTag, NewCount);

	EvaluateStage(QuestTag);
}

void URPG_ObjectiveTrackerComponent::SetObjectiveProgress(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 AbsoluteCount)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	URPG_QuestLogComponent* Log = GetQuestLog();
	if (!Log)
	{
		return;
	}
	// Compute the delta against the log's current counter (AdvanceObjective is delta-based).
	const int32 Current = Log->GetObjectiveCount(QuestTag, ObjectiveTag);
	const int32 Delta = AbsoluteCount - Current;
	if (Delta != 0)
	{
		const int32 NewCount = Log->AdvanceObjective(QuestTag, ObjectiveTag, Delta);
		BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_ObjectiveProgress, QuestTag, ObjectiveTag, NewCount);
	}
	EvaluateStage(QuestTag);
}

void URPG_ObjectiveTrackerComponent::RevealHiddenObjective(FGameplayTag QuestTag, FGameplayTag ObjectiveTag)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	const int32 Index = FindStateIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FRPG_QuestStageState& State = StageStates.Entries[Index];
	if (!State.RevealedHiddenObjectives.HasTagExact(ObjectiveTag))
	{
		State.RevealedHiddenObjectives.AddTag(ObjectiveTag);
		MarkStateDirtyAndNotify(State, /*bStageChanged=*/false);
	}
}

void URPG_ObjectiveTrackerComponent::RequestFailQuest(FGameplayTag QuestTag)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	const int32 Index = FindStateIndex(QuestTag);
	const FRPG_QuestStage* StageDef = nullptr;
	if (Index != INDEX_NONE)
	{
		if (URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag))
		{
			StageDef = Graph->FindStage(StageStates.Entries[Index].CurrentStage);
		}
	}

	// A failing stage with a FailToStage routes there instead of failing the whole quest.
	if (StageDef && StageDef->FailToStage.IsValid())
	{
		EnterStage(QuestTag, StageDef->FailToStage);
		return;
	}

	DeactivateObjectives();
	if (URPG_QuestLogComponent* Log = GetQuestLog())
	{
		Log->FailQuest(QuestTag);
	}
	BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_Failed, QuestTag, FGameplayTag(), 0);

	if (Index != INDEX_NONE)
	{
		StageStates.Entries.RemoveAt(Index);
		StageStates.MarkArrayDirty();
	}
}

FGameplayTag URPG_ObjectiveTrackerComponent::GetCurrentStage(FGameplayTag QuestTag) const
{
	const int32 Index = FindStateIndex(QuestTag);
	return Index != INDEX_NONE ? StageStates.Entries[Index].CurrentStage : FGameplayTag();
}

bool URPG_ObjectiveTrackerComponent::IsObjectiveHidden(FGameplayTag QuestTag, FGameplayTag ObjectiveTag) const
{
	const int32 Index = FindStateIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FRPG_QuestStageState& State = StageStates.Entries[Index];
	if (State.RevealedHiddenObjectives.HasTagExact(ObjectiveTag))
	{
		return false; // revealed -> visible
	}
	const URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	const FRPG_QuestStage* StageDef = Graph ? Graph->FindStage(State.CurrentStage) : nullptr;
	if (!StageDef)
	{
		return false;
	}
	for (const FRPG_QuestStageObjective& Obj : StageDef->Objectives)
	{
		if (Obj.Objective.ObjectiveTag == ObjectiveTag)
		{
			return Obj.bHidden;
		}
	}
	return false;
}

float URPG_ObjectiveTrackerComponent::GetStageTimeRemaining(FGameplayTag QuestTag) const
{
	const int32 Index = FindStateIndex(QuestTag);
	if (Index == INDEX_NONE || !GetWorld())
	{
		return -1.f;
	}
	const FRPG_QuestStageState& State = StageStates.Entries[Index];
	const URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	const FRPG_QuestStage* StageDef = Graph ? Graph->FindStage(State.CurrentStage) : nullptr;
	if (!StageDef || StageDef->TimeLimitSeconds <= 0.f)
	{
		return -1.f;
	}
	const double Elapsed = GetWorld()->GetTimeSeconds() - State.StageStartWorldTime;
	return FMath::Max(0.f, StageDef->TimeLimitSeconds - static_cast<float>(Elapsed));
}

// ===== Stage flow ================================================================================

void URPG_ObjectiveTrackerComponent::EnterStage(const FGameplayTag& QuestTag, const FGameplayTag& NewStage)
{
	URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	const FRPG_QuestStage* StageDef = Graph ? Graph->FindStage(NewStage) : nullptr;
	if (!StageDef)
	{
		UE_LOG(LogDP, Warning, TEXT("[RPG] EnterStage: unknown stage '%s' of quest '%s'."),
			*NewStage.ToString(), *QuestTag.ToString());
		return;
	}

	// Prerequisites must pass to enter.
	for (const FRPG_StageGate& Gate : StageDef->Prerequisites)
	{
		if (!EvaluateGate(QuestTag, Gate))
		{
			UE_LOG(LogDP, Verbose, TEXT("[RPG] Stage '%s' prerequisites not met."), *NewStage.ToString());
			return;
		}
	}

	DeactivateObjectives();

	const int32 Index = FindStateIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FRPG_QuestStageState& State = StageStates.Entries[Index];
	State.CurrentStage = NewStage;
	State.RevealedHiddenObjectives.Reset();
	State.VisitedStages.AddTag(NewStage);
	State.StageStartWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// Record a hub "visited stage" flag so future prerequisite RequiredPriorStage checks resolve.
	{
		FRPG_HubWrite VisitWrite;
		VisitWrite.Key = NewStage;
		VisitWrite.bIsCounter = false;
		VisitWrite.bFlagValue = true;
		VisitWrite.ScopeKind = ERPG_HubScopeKind::Global;
		ApplyHubWrite(VisitWrite);
	}

	MarkStateDirtyAndNotify(State, /*bStageChanged=*/true);
	BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_StageAdvanced, QuestTag, NewStage, 0);

	ActivateStageObjectives(QuestTag, *StageDef);

	// Refresh whether any active stage carries a time limit (drives the tick).
	bAnyTimeLimit = false;
	for (const FRPG_QuestStageState& Entry : StageStates.Entries)
	{
		const URPG_QuestGraphDefinition* G = ResolveGraph(Entry.QuestTag);
		const FRPG_QuestStage* S = G ? G->FindStage(Entry.CurrentStage) : nullptr;
		if ((S && S->TimeLimitSeconds > 0.f) || (G && G->QuestTimeLimitSeconds > 0.f))
		{
			bAnyTimeLimit = true;
			break;
		}
	}
	SetComponentTickEnabled(bAnyTimeLimit);

	// A freshly-entered stage may already be complete (e.g. all objectives are CollectItem already held).
	EvaluateStage(QuestTag);
}

void URPG_ObjectiveTrackerComponent::DeactivateObjectives()
{
	for (TObjectPtr<URPG_Objective>& Obj : ActiveObjectives)
	{
		if (Obj)
		{
			Obj->EndTracking();
		}
	}
	ActiveObjectives.Reset();
}

void URPG_ObjectiveTrackerComponent::ActivateStageObjectives(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef)
{
	URPG_QuestLogComponent* Log = GetQuestLog();
	for (const FRPG_QuestStageObjective& Slot : StageDef.Objectives)
	{
		const FGameplayTag ObjTag = Slot.Objective.ObjectiveTag;
		if (!ObjTag.IsValid())
		{
			continue;
		}

		// Seed the log counter to 0 so this stage's objectives are "known" (AdvanceObjective by 0 ensures
		// an entry exists without advancing). The base log creates the counter lazily; we touch it.
		if (Log)
		{
			Log->AdvanceObjective(QuestTag, ObjTag, 0);
		}

		if (URPG_Objective* Tracker = Slot.Tracker)
		{
			ActiveObjectives.Add(Tracker);
			Tracker->BeginTracking(this, QuestTag, ObjTag);
		}
	}
}

void URPG_ObjectiveTrackerComponent::EvaluateStage(const FGameplayTag& QuestTag)
{
	URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	const int32 Index = FindStateIndex(QuestTag);
	if (!Graph || Index == INDEX_NONE)
	{
		return;
	}
	const FRPG_QuestStage* StageDef = Graph->FindStage(StageStates.Entries[Index].CurrentStage);
	if (!StageDef)
	{
		return;
	}

	if (!IsStageComplete(QuestTag, *StageDef))
	{
		return;
	}

	// Stage complete -> fire the first satisfiable branch outcome. If none fires and there are no outcomes
	// at all, completing the final stage completes the quest.
	if (!TryFireOutcome(QuestTag, *StageDef))
	{
		if (StageDef->Outcomes.Num() == 0)
		{
			DeactivateObjectives();
			if (URPG_QuestLogComponent* Log = GetQuestLog())
			{
				Log->CompleteQuest(QuestTag);
			}
			BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_Completed, QuestTag, FGameplayTag(), 0);
			const int32 RemoveAt = FindStateIndex(QuestTag);
			if (RemoveAt != INDEX_NONE)
			{
				StageStates.Entries.RemoveAt(RemoveAt);
				StageStates.MarkArrayDirty();
			}
		}
	}
}

bool URPG_ObjectiveTrackerComponent::IsStageComplete(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef) const
{
	const URPG_QuestLogComponent* Log = GetQuestLog();
	if (!Log)
	{
		return false;
	}

	for (const FRPG_QuestStageObjective& Slot : StageDef.Objectives)
	{
		const FGameplayTag ObjTag = Slot.Objective.ObjectiveTag;
		if (!ObjTag.IsValid())
		{
			continue;
		}
		const bool bObjComplete = Log->GetObjectiveCount(QuestTag, ObjTag) >= FMath::Max(1, Slot.Objective.RequiredCount);

		if (StageDef.Completion == ERPG_StageLogic::Any)
		{
			// OR: any objective (optional or required) completing finishes the stage.
			if (bObjComplete)
			{
				return true;
			}
		}
		else if (!Slot.bOptional && !bObjComplete)
		{
			// AND: a required objective that is not yet complete blocks the stage.
			return false;
		}
	}

	if (StageDef.Completion == ERPG_StageLogic::Any)
	{
		return false; // no objective satisfied yet
	}
	// AND: reached the end with no incomplete required objective -> stage complete. (A stage with no
	// required objectives is a vacuously-complete pure branch/effect stage.)
	return true;
}

bool URPG_ObjectiveTrackerComponent::TryFireOutcome(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef)
{
	for (const FRPG_QuestBranchOutcome& Outcome : StageDef.Outcomes)
	{
		if (Outcome.When.IsEmpty() || EvaluateGate(QuestTag, Outcome.When))
		{
			ApplyOutcome(QuestTag, Outcome);
			return true;
		}
	}
	return false;
}

void URPG_ObjectiveTrackerComponent::ApplyOutcome(const FGameplayTag& QuestTag, const FRPG_QuestBranchOutcome& Outcome)
{
	for (const FRPG_HubWrite& Write : Outcome.Effects)
	{
		ApplyHubWrite(Write);
	}

	if (Outcome.bFailsQuest)
	{
		RequestFailQuest(QuestTag);
		return;
	}
	if (Outcome.bCompletesQuest)
	{
		DeactivateObjectives();
		if (URPG_QuestLogComponent* Log = GetQuestLog())
		{
			Log->CompleteQuest(QuestTag);
		}
		BroadcastQuestEvent(RPG_QuestNativeTags::Bus_RPG_Quest_Completed, QuestTag, FGameplayTag(), 0);
		const int32 Index = FindStateIndex(QuestTag);
		if (Index != INDEX_NONE)
		{
			StageStates.Entries.RemoveAt(Index);
			StageStates.MarkArrayDirty();
		}
		return;
	}
	if (Outcome.NextStage.IsValid())
	{
		EnterStage(QuestTag, Outcome.NextStage);
	}
}

// ===== Gate evaluation (RPG-local, fail-closed) ==================================================

bool URPG_ObjectiveTrackerComponent::EvaluateGate(const FGameplayTag& QuestTag, const FRPG_StageGate& Gate) const
{
	if (Gate.IsEmpty())
	{
		return true;
	}

	UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);

	// Flag sub-check.
	if (Gate.HubFlagKey.IsValid())
	{
		const bool bActual = Hub ? Hub->QueryFlag(Gate.HubFlagKey, FWorldHub_Scope::Global(), false) : false;
		if (bActual != Gate.bFlagExpected)
		{
			return false;
		}
	}

	// Counter sub-check.
	if (Gate.CounterKey.IsValid())
	{
		const int64 Value = Hub ? Hub->QueryCounter(Gate.CounterKey, FWorldHub_Scope::Global(), 0) : 0;
		bool bPass = false;
		switch (Gate.CounterCompare)
		{
		case ERPG_CounterCompare::Less:         bPass = Value <  Gate.CounterThreshold; break;
		case ERPG_CounterCompare::LessEqual:    bPass = Value <= Gate.CounterThreshold; break;
		case ERPG_CounterCompare::Equal:        bPass = Value == Gate.CounterThreshold; break;
		case ERPG_CounterCompare::GreaterEqual: bPass = Value >= Gate.CounterThreshold; break;
		case ERPG_CounterCompare::Greater:      bPass = Value >  Gate.CounterThreshold; break;
		case ERPG_CounterCompare::NotEqual:     bPass = Value != Gate.CounterThreshold; break;
		default:                                bPass = false; break;
		}
		if (!bPass)
		{
			return false;
		}
	}

	// Prior-stage sub-check (visited-stage flag stored Global on EnterStage).
	if (Gate.RequiredPriorStage.IsValid())
	{
		const bool bVisited = Hub ? Hub->QueryFlag(Gate.RequiredPriorStage, FWorldHub_Scope::Global(), false) : false;
		if (!bVisited)
		{
			return false;
		}
	}

	// Reputation sub-check (fail-closed: an absent provider fails a reputation gate).
	if (Gate.ReputationTag.IsValid())
	{
		ISeam_Reputation* Rep = nullptr;
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			static const FGameplayTag RepService =
				FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Narrative.Reputation"), /*bErrorIfNotFound=*/false);
			if (UObject* Provider = Locator->ResolveService(RepService))
			{
				Rep = Cast<ISeam_Reputation>(Provider);
			}
		}
		if (!Rep || !Rep->MeetsStanding(GetOwner(), Gate.ReputationTag, Gate.MinReputation))
		{
			return false;
		}
	}

	return true;
}

void URPG_ObjectiveTrackerComponent::ApplyHubWrite(const FRPG_HubWrite& Write) const
{
	if (!Write.Key.IsValid())
	{
		return;
	}
	UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (!Hub)
	{
		return;
	}

	FWorldHub_Scope Scope = FWorldHub_Scope::Global();
	switch (Write.ScopeKind)
	{
	case ERPG_HubScopeKind::Faction:
		Scope = FWorldHub_Scope::Faction(Write.FactionTag);
		break;
	case ERPG_HubScopeKind::OwnerEntity:
	{
		const FGuid Guid = ResolveOwnerEntityGuid();
		if (Guid.IsValid())
		{
			Scope = FWorldHub_Scope::Entity(FSeam_EntityId(Guid));
		}
		break;
	}
	case ERPG_HubScopeKind::Global:
	default:
		break;
	}

	if (Write.bIsCounter)
	{
		Hub->IncrementCounter(Write.Key, Write.CounterDelta, Scope);
	}
	else
	{
		Hub->SetFlag(Write.Key, Write.bFlagValue, Scope);
	}
}

// ===== Time limits ===============================================================================

void URPG_ObjectiveTrackerComponent::TickTimeLimits()
{
	if (!GetWorld())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();

	// Iterate a copy of quest tags because failing/advancing a quest mutates Entries.
	TArray<FGameplayTag> Quests;
	Quests.Reserve(StageStates.Entries.Num());
	for (const FRPG_QuestStageState& Entry : StageStates.Entries)
	{
		Quests.Add(Entry.QuestTag);
	}

	for (const FGameplayTag& QuestTag : Quests)
	{
		const int32 Index = FindStateIndex(QuestTag);
		if (Index == INDEX_NONE)
		{
			continue;
		}
		const FRPG_QuestStageState State = StageStates.Entries[Index]; // copy (may be removed below)
		URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
		if (!Graph)
		{
			continue;
		}

		// Overall quest limit.
		if (Graph->QuestTimeLimitSeconds > 0.f
			&& (Now - State.QuestStartWorldTime) >= Graph->QuestTimeLimitSeconds)
		{
			RequestFailQuest(QuestTag);
			continue;
		}

		// Per-stage limit.
		const FRPG_QuestStage* StageDef = Graph->FindStage(State.CurrentStage);
		if (StageDef && StageDef->TimeLimitSeconds > 0.f
			&& (Now - State.StageStartWorldTime) >= StageDef->TimeLimitSeconds)
		{
			if (StageDef->FailToStage.IsValid())
			{
				EnterStage(QuestTag, StageDef->FailToStage);
			}
			else
			{
				RequestFailQuest(QuestTag);
			}
		}
	}
}

// ===== Replication notification + save ===========================================================

void URPG_ObjectiveTrackerComponent::MarkStateDirtyAndNotify(FRPG_QuestStageState& Entry, bool bStageChanged)
{
	StageStates.MarkItemDirty(Entry);
	if (bStageChanged)
	{
		OnStageAdvanced.Broadcast(Entry.QuestTag, Entry.CurrentStage);
	}
}

void URPG_ObjectiveTrackerComponent::HandleStageReplicated(const FGameplayTag& QuestTag, const FGameplayTag& NewStage)
{
	// Client-side surface of a replicated stage change (cosmetic; authoritative logic already ran on server).
	OnStageAdvanced.Broadcast(QuestTag, NewStage);
}

void URPG_ObjectiveTrackerComponent::BroadcastQuestEvent(const FGameplayTag& Channel, const FGameplayTag& QuestTag, const FGameplayTag& NodeTag, int32 Value) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus || !Channel.IsValid())
	{
		return;
	}
	const FRPG_QuestBusEvent Payload(QuestTag, NodeTag, Value);
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload), const_cast<URPG_ObjectiveTrackerComponent*>(this));
}

TArray<FRPG_QuestStageState> URPG_ObjectiveTrackerComponent::ExportStageStates() const
{
	// Persist remaining time as a relative offset by re-anchoring world-time fields at restore. We store the
	// raw entries here; the save game converts elapsed->remaining. To keep it self-contained, we re-encode
	// the world-time fields as ELAPSED seconds so restore can re-anchor to its own "now".
	TArray<FRPG_QuestStageState> Out = StageStates.Entries;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (FRPG_QuestStageState& Entry : Out)
	{
		Entry.StageStartWorldTime = Now - Entry.StageStartWorldTime; // -> elapsed in stage
		Entry.QuestStartWorldTime = Now - Entry.QuestStartWorldTime; // -> elapsed in quest
	}
	return Out;
}

void URPG_ObjectiveTrackerComponent::ImportStageStates(const TArray<FRPG_QuestStageState>& InStates)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	DeactivateObjectives();
	StageStates.Entries.Reset();

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	bAnyTimeLimit = false;

	for (const FRPG_QuestStageState& Saved : InStates)
	{
		FRPG_QuestStageState Entry = Saved;
		// Re-anchor: saved fields are ELAPSED seconds; convert back to an absolute start time at "now".
		Entry.StageStartWorldTime = Now - Saved.StageStartWorldTime;
		Entry.QuestStartWorldTime = Now - Saved.QuestStartWorldTime;
		StageStates.Entries.Add(Entry);
	}
	StageStates.MarkArrayDirty();

	// Re-activate each restored stage's objective evaluators.
	for (const FRPG_QuestStageState& Entry : StageStates.Entries)
	{
		URPG_QuestGraphDefinition* Graph = ResolveGraph(Entry.QuestTag);
		const FRPG_QuestStage* StageDef = Graph ? Graph->FindStage(Entry.CurrentStage) : nullptr;
		if (StageDef)
		{
			ActivateStageObjectives(Entry.QuestTag, *StageDef);
			if (StageDef->TimeLimitSeconds > 0.f || (Graph && Graph->QuestTimeLimitSeconds > 0.f))
			{
				bAnyTimeLimit = true;
			}
		}
	}
	SetComponentTickEnabled(bAnyTimeLimit);
}
