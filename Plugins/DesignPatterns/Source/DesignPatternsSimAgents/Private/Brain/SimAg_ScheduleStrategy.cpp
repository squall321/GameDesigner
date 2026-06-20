// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_ScheduleStrategy.h"
#include "Brain/SimAg_Agent.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Schedule/SimAg_Scheduler.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Resolve an ISimAg_Scheduler off Actor (the actor or one of its components). */
	TScriptInterface<ISimAg_Scheduler> ResolveScheduler(AActor* Actor)
	{
		auto Consider = [](UObject* Candidate) -> TScriptInterface<ISimAg_Scheduler>
		{
			if (Candidate && Candidate->Implements<USimAg_Scheduler>())
			{
				TScriptInterface<ISimAg_Scheduler> Result;
				Result.SetObject(Candidate);
				Result.SetInterface(Cast<ISimAg_Scheduler>(Candidate));
				return Result;
			}
			return TScriptInterface<ISimAg_Scheduler>();
		};

		if (!Actor)
		{
			return TScriptInterface<ISimAg_Scheduler>();
		}
		if (TScriptInterface<ISimAg_Scheduler> FromActor = Consider(Actor))
		{
			return FromActor;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (TScriptInterface<ISimAg_Scheduler> FromComp = Consider(Comp))
			{
				return FromComp;
			}
		}
		return TScriptInterface<ISimAg_Scheduler>();
	}

	USimAg_AgentComponent* ResolveAgentComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<USimAg_AgentComponent>() : nullptr;
	}

	FVector ResolveTargetLocation(AActor* Actor, ESimAg_NeedTargetSource Source, const FVector& Override)
	{
		switch (Source)
		{
		case ESimAg_NeedTargetSource::ExplicitTarget:
			return Override;
		case ESimAg_NeedTargetSource::CurrentLocation:
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		case ESimAg_NeedTargetSource::Home:
			if (Actor && Actor->Implements<USimAg_Agent>())
			{
				return ISimAg_Agent::Execute_GetHomeLocation(Actor);
			}
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		case ESimAg_NeedTargetSource::Work:
		default:
			if (Actor && Actor->Implements<USimAg_Agent>())
			{
				return ISimAg_Agent::Execute_GetWorkLocation(Actor);
			}
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		}
	}
}

USimAg_ScheduleStrategy::USimAg_ScheduleStrategy()
{
	DebugName = TEXT("Schedule");
}

float USimAg_ScheduleStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !MatchActivity.IsValid())
	{
		return 0.f;
	}

	// SIDE-EFFECT-FREE: resolve the scheduler fresh and read the current scheduled activity.
	const TScriptInterface<ISimAg_Scheduler> Scheduler = ResolveScheduler(Owner);
	if (!Scheduler)
	{
		return 0.f;
	}

	const FGameplayTag Current = ISimAg_Scheduler::Execute_GetCurrentActivity(Scheduler.GetObject());
	// Hierarchy-aware match: a scheduled "Work.Forge" satisfies a strategy keyed on "Work".
	if (!Current.IsValid() || !Current.MatchesTag(MatchActivity))
	{
		return 0.f;
	}
	return FMath::Max(0.f, MatchScore);
}

void USimAg_ScheduleStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	const FVector Target = ResolveTargetLocation(Owner, TargetSource, TargetOverride);
	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, Target);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
	}

	// Set the agent's current activity to the scheduled one (authority-guarded inside).
	if (USimAg_AgentComponent* Agent = ResolveAgentComponent(Owner))
	{
		const TScriptInterface<ISimAg_Scheduler> Scheduler = ResolveScheduler(Owner);
		const FGameplayTag Scheduled = Scheduler
			? ISimAg_Scheduler::Execute_GetCurrentActivity(Scheduler.GetObject())
			: MatchActivity;
		Agent->SetCurrentActivity(Scheduled.IsValid() ? Scheduled : MatchActivity);
	}
}
