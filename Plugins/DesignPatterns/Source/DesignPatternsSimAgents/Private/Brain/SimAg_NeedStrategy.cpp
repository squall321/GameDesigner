// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_NeedStrategy.h"
#include "Brain/SimAg_Agent.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Needs/Seam_NeedProvider.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * Resolve an ISeam_NeedProvider that supports NeedTag off Actor (the actor itself or one of its
	 * components — a brain may compose several providers, so we pick the one that owns the need). Returns
	 * an invalid interface when none answers the need.
	 */
	TScriptInterface<ISeam_NeedProvider> ResolveNeedProvider(AActor* Actor, const FGameplayTag& NeedTag)
	{
		if (!Actor || !NeedTag.IsValid())
		{
			return TScriptInterface<ISeam_NeedProvider>();
		}

		auto Consider = [&NeedTag](UObject* Candidate) -> TScriptInterface<ISeam_NeedProvider>
		{
			if (Candidate && Candidate->Implements<USeam_NeedProvider>())
			{
				if (ISeam_NeedProvider::Execute_SupportsNeed(Candidate, NeedTag))
				{
					TScriptInterface<ISeam_NeedProvider> Result;
					Result.SetObject(Candidate);
					Result.SetInterface(Cast<ISeam_NeedProvider>(Candidate));
					return Result;
				}
			}
			return TScriptInterface<ISeam_NeedProvider>();
		};

		if (TScriptInterface<ISeam_NeedProvider> FromActor = Consider(Actor))
		{
			return FromActor;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (TScriptInterface<ISeam_NeedProvider> FromComp = Consider(Comp))
			{
				return FromComp;
			}
		}
		return TScriptInterface<ISeam_NeedProvider>();
	}

	/** Find the agent component off an actor (the actor or a component implementing ISimAg_Agent). */
	USimAg_AgentComponent* ResolveAgentComponent(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<USimAg_AgentComponent>() : nullptr;
	}

	/** Resolve the move target from a target source, reading the agent seam's anchors as needed. */
	FVector ResolveTargetLocation(AActor* Actor, ESimAg_NeedTargetSource Source, const FVector& Override)
	{
		switch (Source)
		{
		case ESimAg_NeedTargetSource::ExplicitTarget:
			return Override;
		case ESimAg_NeedTargetSource::CurrentLocation:
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		case ESimAg_NeedTargetSource::Work:
			if (Actor && Actor->Implements<USimAg_Agent>())
			{
				return ISimAg_Agent::Execute_GetWorkLocation(Actor);
			}
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		case ESimAg_NeedTargetSource::Home:
		default:
			if (Actor && Actor->Implements<USimAg_Agent>())
			{
				return ISimAg_Agent::Execute_GetHomeLocation(Actor);
			}
			return Actor ? Actor->GetActorLocation() : FVector::ZeroVector;
		}
	}
}

USimAg_NeedStrategy::USimAg_NeedStrategy()
{
	DebugName = TEXT("Need");
	// Default urgency curve: linear from (0 urgency -> 0 score) to (1 urgency -> 1 score). Designers
	// reshape it; this is a sane identity mapping, not a hidden magic weight.
	if (FRichCurve* Curve = UrgencyCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(0.f, 0.f);
		Curve->AddKey(1.f, 1.f);
	}
}

float USimAg_NeedStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !NeedTag.IsValid())
	{
		return 0.f;
	}

	// SIDE-EFFECT-FREE: resolve the need provider fresh each evaluation and read the satisfaction.
	const TScriptInterface<ISeam_NeedProvider> Provider = ResolveNeedProvider(Owner, NeedTag);
	if (!Provider)
	{
		return 0.f; // no provider answers this need on this agent
	}

	const float Satisfaction = FMath::Clamp(
		ISeam_NeedProvider::Execute_GetNeedNormalized(Provider.GetObject(), NeedTag), 0.f, 1.f);
	const float Urgency = 1.f - Satisfaction;

	const FRichCurve* Curve = UrgencyCurve.GetRichCurveConst();
	const float Score = Curve ? Curve->Eval(Urgency) : Urgency;
	return FMath::Max(0.f, Score);
}

void USimAg_NeedStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	// Write the chosen destination into the blackboard for steering to consume.
	const FVector Target = ResolveTargetLocation(Owner, TargetSource, TargetOverride);
	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, Target);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
	}

	// Reflect the activity on the agent (authority-guarded inside SetCurrentActivity).
	if (ActivityTag.IsValid())
	{
		if (USimAg_AgentComponent* Agent = ResolveAgentComponent(Owner))
		{
			Agent->SetCurrentActivity(ActivityTag);
		}
	}
}
