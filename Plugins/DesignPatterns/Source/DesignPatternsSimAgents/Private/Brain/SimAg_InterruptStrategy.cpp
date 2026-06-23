// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_InterruptStrategy.h"
#include "Brain/SimAg_StrategySupport.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Memory/SimAg_MemoryComponent.h"
#include "Routine/SimAg_RoutineComponent.h"
#include "FSM/DPBlackboard.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * Detect a threat near the agent. Prefer the live ISeam_ThreatSense seam; fall back to a remembered
	 * Threat fact in memory. Returns true and fills OutThreatLoc / OutSeverity when a threat is present.
	 */
	bool DetectThreat(
		AActor* Owner, const FGameplayTag& ThreatServiceKey, const FGameplayTag& ThreatMemoryKind,
		float SenseRadius, FVector& OutThreatLoc, float& OutSeverity)
	{
		const FVector AgentLoc = Owner->GetActorLocation();

		// 1) Live threat sense.
		const TScriptInterface<ISeam_ThreatSense> Sense = SimAg_StrategySupport::ResolveThreatSense(Owner, ThreatServiceKey);
		if (Sense)
		{
			FVector ThreatLoc = AgentLoc;
			float Severity = 0.f;
			if (ISeam_ThreatSense::Execute_QueryThreat(Sense.GetObject(), AgentLoc, SenseRadius, ThreatLoc, Severity))
			{
				OutThreatLoc = ThreatLoc;
				OutSeverity = FMath::Clamp(Severity, 0.f, 1.f);
				return true;
			}
		}

		// 2) Remembered threat fact (confidence acts as severity).
		if (ThreatMemoryKind.IsValid())
		{
			if (const USimAg_MemoryComponent* Memory = Owner->FindComponentByClass<USimAg_MemoryComponent>())
			{
				FSimAg_MemoryFact Fact;
				if (Memory->QueryNearest(ThreatMemoryKind, AgentLoc, Fact))
				{
					// Only react to a remembered threat that is actually nearby.
					if (FVector::Dist(AgentLoc, Fact.WorldLocation) <= SenseRadius)
					{
						OutThreatLoc = Fact.WorldLocation;
						OutSeverity = Memory->GetDecayedConfidenceFor(ThreatMemoryKind);
						return OutSeverity > 0.f;
					}
				}
			}
		}

		return false;
	}
}

USimAg_InterruptStrategy::USimAg_InterruptStrategy()
{
	DebugName = TEXT("Interrupt");
}

float USimAg_InterruptStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return 0.f;
	}

	FVector ThreatLoc;
	float Severity = 0.f;
	if (DetectThreat(Owner, ThreatServiceKey, ThreatMemoryKind, ThreatSenseRadius, ThreatLoc, Severity))
	{
		// Dominate the selector while a threat is present, scaled by severity so a faint threat still wins
		// over ordinary behaviour but a dire one wins by the largest margin.
		return FMath::Max(0.f, ThreatScore * FMath::Max(0.05f, Severity));
	}
	return 0.f;
}

void USimAg_InterruptStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	FVector ThreatLoc;
	float Severity = 0.f;
	USimAg_RoutineComponent* Routine = Owner->FindComponentByClass<USimAg_RoutineComponent>();

	if (DetectThreat(Owner, ThreatServiceKey, ThreatMemoryKind, ThreatSenseRadius, ThreatLoc, Severity))
	{
		// Interrupt the daily routine so it can resume after.
		if (Routine && !Routine->IsInterrupted())
		{
			Routine->Interrupt(InterruptReason);
		}

		// Flee: pick a point FleeDistance away from the threat (directly opposite), preserving Z.
		const FVector AgentLoc = Owner->GetActorLocation();
		FVector Away = AgentLoc - ThreatLoc;
		Away.Z = 0.f;
		if (Away.IsNearlyZero())
		{
			Away = Owner->GetActorForwardVector(); // degenerate: just run forward
			Away.Z = 0.f;
		}
		Away = Away.GetSafeNormal();
		const FVector FleeTarget = AgentLoc + Away * FleeDistance;

		if (Context.Blackboard)
		{
			Context.Blackboard->SetVector(MoveTargetKey, FleeTarget);
			Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
			Context.Blackboard->SetBool(FleeingKey, true);
		}
		if (FleeActivity.IsValid())
		{
			if (USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
			{
				Agent->SetCurrentActivity(FleeActivity);
			}
		}
	}
	else
	{
		// No threat: if we were fleeing, resume the routine.
		const bool bWasFleeing = Context.Blackboard ? Context.Blackboard->GetBool(FleeingKey, false) : false;
		if (bWasFleeing)
		{
			if (Context.Blackboard)
			{
				Context.Blackboard->SetBool(FleeingKey, false);
			}
			if (Routine && Routine->IsInterrupted())
			{
				Routine->Resume();
			}
		}
	}
}
