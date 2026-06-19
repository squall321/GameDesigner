// Copyright DesignPatterns plugin. All Rights Reserved.

#include "FSM/DPState.h"
#include "FSM/DPStateMachineComponent.h"
#include "FSM/DPBlackboard.h"
#include "Strategy/DPStrategy.h"
#include "Strategy/DPStrategySelector.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

bool UDP_TransitionGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	// Default: a present-but-unoverridden guard permits the transition.
	return true;
}

void UDP_State::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	UE_LOG(LogDPFSM, Verbose, TEXT("State '%s' OnEnter"), *StateTag.ToString());
}

void UDP_State::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	// Compose the Strategy pattern: when a selector is authored, let it pick & run behaviour.
	if (StrategySelector && OwningComponent)
	{
		const FDP_StrategyContext Context = OwningComponent->MakeStrategyContext();
		StrategySelector->SelectAndExecute(Context);
	}
}

void UDP_State::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	UE_LOG(LogDPFSM, Verbose, TEXT("State '%s' OnExit"), *StateTag.ToString());
}

bool UDP_State::CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const
{
	return true;
}
