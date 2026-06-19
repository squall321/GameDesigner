// Copyright DesignPatterns plugin. All Rights Reserved.

#include "FSM/DPStateMachineComponent.h"
#include "FSM/DPStateMachineDefinition.h"
#include "FSM/DPState.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "VisualLogger/VisualLogger.h"

DECLARE_CYCLE_STAT(TEXT("FSM Tick"), STAT_DP_FSMTick, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("FSM ChangeState"), STAT_DP_FSMChangeState, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("FSM Transitions"), STAT_DP_FSMTransitions, STATGROUP_DesignPatterns);

UDP_StateMachineComponent::UDP_StateMachineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Only ActiveStateTag is pushed over the wire; the rest is shared deterministic data.
	SetIsReplicatedByDefault(true);
}

void UDP_StateMachineComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!Blackboard)
	{
		// Owned subobject: outer = this so it is GC-rooted by the component and travels with it.
		Blackboard = NewObject<UDP_Blackboard>(this, UDP_Blackboard::StaticClass(), TEXT("Blackboard"));
	}

	// The authority drives state; clients receive ActiveStateTag via replication/OnRep.
	if (GetOwnerRole() == ROLE_Authority)
	{
		RestartToInitialState();
	}
}

void UDP_StateMachineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Give the active state a chance to clean up.
	if (UDP_State* Active = GetActiveState())
	{
		Active->OnExit(this);
	}

	Super::EndPlay(EndPlayReason);
}

void UDP_StateMachineComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate ONLY the active tag — minimal, deterministic network footprint.
	DOREPLIFETIME(UDP_StateMachineComponent, ActiveStateTag);
}

void UDP_StateMachineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	SCOPE_CYCLE_COUNTER(STAT_DP_FSMTick);

	UDP_State* Active = GetActiveState();
	if (!Active)
	{
		return;
	}

	Active->OnTick(this, DeltaTime);

	// Transition evaluation is authority-only; clients follow via replicated ActiveStateTag.
	if (bEvaluateTransitionsOnTick && GetOwnerRole() == ROLE_Authority)
	{
		EvaluateTransitions();
	}
}

void UDP_StateMachineComponent::RestartToInitialState()
{
	if (!Definition)
	{
		UE_LOG(LogDPFSM, Warning, TEXT("%s: RestartToInitialState with no Definition."), *GetNameSafe(GetOwner()));
		return;
	}

	const FGameplayTag Initial = Definition->InitialStateTag;
	if (!Definition->HasState(Initial))
	{
		UE_LOG(LogDPFSM, Warning, TEXT("%s: InitialStateTag '%s' not found in definition '%s'."),
			*GetNameSafe(GetOwner()), *Initial.ToString(), *GetNameSafe(Definition));
		return;
	}

	// Force entry: the initial state must not be blocked by admission checks.
	ChangeState(Initial, /*bForce=*/true);
}

bool UDP_StateMachineComponent::ChangeState(FGameplayTag NewStateTag, bool bForce)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_FSMChangeState);

	// ActiveStateTag is a replicated, server-authoritative property. Reject any client-side
	// mutation (this function is BlueprintCallable, so game/UI code could call it on a client):
	// clients only ever mirror state via OnRep_ActiveStateTag. Standalone games are ROLE_Authority.
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogDPFSM, Verbose, TEXT("%s: ChangeState ignored on non-authority."), *GetNameSafe(GetOwner()));
		return false;
	}

	if (!Definition)
	{
		UE_LOG(LogDPFSM, Warning, TEXT("%s: ChangeState with no Definition."), *GetNameSafe(GetOwner()));
		return false;
	}

	if (NewStateTag == ActiveStateTag)
	{
		return false;
	}

	UDP_State* NewState = Definition->FindState(NewStateTag);
	if (!NewState)
	{
		UE_LOG(LogDPFSM, Warning, TEXT("%s: ChangeState to unknown state '%s'."),
			*GetNameSafe(GetOwner()), *NewStateTag.ToString());
		return false;
	}

	if (!PassesEntryAdmission(NewState, bForce))
	{
		UE_LOG(LogDPFSM, Verbose, TEXT("%s: entry to '%s' denied by CanEnter."),
			*GetNameSafe(GetOwner()), *NewStateTag.ToString());
		return false;
	}

	const FGameplayTag PreviousTag = ActiveStateTag;

	// Authority mutates the replicated property; OnRep does NOT fire on the server, so we apply
	// the hooks here directly. Clients will run ApplyStateChange from OnRep_ActiveStateTag.
	ActiveStateTag = NewStateTag;
	ApplyStateChange(PreviousTag, NewStateTag);

	INC_DWORD_STAT(STAT_DP_FSMTransitions);
	return true;
}

void UDP_StateMachineComponent::OnRep_ActiveStateTag(FGameplayTag PreviousStateTag)
{
	// Client mirror of an authoritative change: run exit/enter hooks and fire the delegate.
	ApplyStateChange(PreviousStateTag, ActiveStateTag);
}

void UDP_StateMachineComponent::ApplyStateChange(FGameplayTag PreviousTag, FGameplayTag NewTag)
{
	if (!Definition)
	{
		return;
	}

	if (UDP_State* PrevState = Definition->FindState(PreviousTag))
	{
		PrevState->OnExit(this);
		UE_VLOG(GetOwner(), LogDPFSM, Verbose, TEXT("FSM exit '%s'"), *PreviousTag.ToString());
	}

	if (UDP_State* NewState = Definition->FindState(NewTag))
	{
		NewState->OnEnter(this);
		UE_VLOG(GetOwner(), LogDPFSM, Log, TEXT("FSM enter '%s' (from '%s')"),
			*NewTag.ToString(), *PreviousTag.ToString());
	}

	OnStateChanged.Broadcast(PreviousTag, NewTag);
}

void UDP_StateMachineComponent::EvaluateTransitions()
{
	UDP_State* Active = GetActiveState();
	if (!Active || Active->Transitions.Num() == 0)
	{
		return;
	}

	// Evaluate by descending priority; first eligible edge wins. Copy indices so the authored
	// array order is preserved as a stable tie-break (stable sort by -Priority).
	TArray<const FDP_StateTransition*> Ordered;
	Ordered.Reserve(Active->Transitions.Num());
	for (const FDP_StateTransition& Transition : Active->Transitions)
	{
		Ordered.Add(&Transition);
	}
	// With an explicit predicate, TArray<const T*>::StableSort passes the element type (pointers).
	Ordered.StableSort([](const FDP_StateTransition& A, const FDP_StateTransition& B)
	{
		return A.Priority > B.Priority;
	});

	for (const FDP_StateTransition* Transition : Ordered)
	{
		if (!Transition->ToState.IsValid())
		{
			continue;
		}

		// A null guard means "always eligible". A present guard must pass.
		const bool bGuardPasses = (Transition->Guard == nullptr) || Transition->Guard->EvaluateGuard(this);
		if (!bGuardPasses)
		{
			continue;
		}

		// The destination still has to admit entry via CanEnter.
		if (ChangeState(Transition->ToState, /*bForce=*/false))
		{
			return; // one transition per evaluation pass
		}
	}
}

bool UDP_StateMachineComponent::PassesEntryAdmission(UDP_State* NewState, bool bForce) const
{
	if (bForce || !NewState)
	{
		return true;
	}
	return NewState->CanEnter(const_cast<UDP_StateMachineComponent*>(this));
}

UDP_State* UDP_StateMachineComponent::GetActiveState() const
{
	return Definition ? Definition->FindState(ActiveStateTag) : nullptr;
}

FDP_StrategyContext UDP_StateMachineComponent::MakeStrategyContext() const
{
	TScriptInterface<IDP_BlackboardProvider> Provider;
	if (Blackboard)
	{
		Provider.SetObject(Blackboard);
		Provider.SetInterface(Cast<IDP_BlackboardProvider>(Blackboard));
	}
	return FDP_StrategyContext(GetOwner(), Provider);
}

FString UDP_StateMachineComponent::GetDebugString() const
{
	const UDP_State* Active = GetActiveState();
	return FString::Printf(TEXT("FSM[def=%s active=%s transitions=%d %s]"),
		*GetNameSafe(Definition),
		*ActiveStateTag.ToString(),
		Active ? Active->Transitions.Num() : 0,
		Blackboard ? *Blackboard->ToDebugString() : TEXT("BB[null]"));
}
