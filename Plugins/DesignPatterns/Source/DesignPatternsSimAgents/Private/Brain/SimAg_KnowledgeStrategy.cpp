// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_KnowledgeStrategy.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Memory/SimAg_MemoryComponent.h"
#include "FSM/DPBlackboard.h"
#include "GameFramework/Actor.h"

USimAg_KnowledgeStrategy::USimAg_KnowledgeStrategy()
{
	DebugName = TEXT("Knowledge");
	// Default confidence curve: identity (confidence == score). Designers reshape it.
	if (FRichCurve* Curve = ConfidenceCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(0.f, 0.f);
		Curve->AddKey(1.f, 1.f);
	}
}

float USimAg_KnowledgeStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !SubjectKind.IsValid())
	{
		return 0.f;
	}

	const USimAg_MemoryComponent* Memory = Owner->FindComponentByClass<USimAg_MemoryComponent>();
	if (!Memory)
	{
		return 0.f;
	}

	// SIDE-EFFECT-FREE: read the strongest non-faded fact about SubjectKind.
	const float Confidence = Memory->GetDecayedConfidenceFor(SubjectKind);
	if (Confidence <= 0.f)
	{
		return 0.f;
	}

	const FRichCurve* Curve = ConfidenceCurve.GetRichCurveConst();
	const float Score = Curve ? Curve->Eval(Confidence) : Confidence;
	return FMath::Max(0.f, Score);
}

void USimAg_KnowledgeStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	const USimAg_MemoryComponent* Memory = Owner->FindComponentByClass<USimAg_MemoryComponent>();
	if (!Memory)
	{
		return;
	}

	// Prefer the nearest confident fact so the agent walks to the closest known instance.
	FSimAg_MemoryFact Fact;
	if (!Memory->QueryNearest(SubjectKind, Owner->GetActorLocation(), Fact))
	{
		// Fall back to the strongest fact regardless of distance.
		if (!Memory->QueryStrongest(SubjectKind, Fact))
		{
			return;
		}
	}

	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, Fact.WorldLocation);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
	}

	if (ActivityTag.IsValid())
	{
		if (USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
		{
			Agent->SetCurrentActivity(ActivityTag);
		}
	}
}
