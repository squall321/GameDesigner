// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_ChainedJobStrategy.h"
#include "Brain/SimAg_StrategySupport.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Jobs/SimAg_JobChainAsset.h"
#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "Jobs/SimAg_SkillComponent.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Blackboard bool key for a step kind, "<Prefix>.<StepKind>". */
	FName MakeCompletedKey(const FName& Prefix, const FGameplayTag& StepKind)
	{
		return FName(*FString::Printf(TEXT("%s.%s"), *Prefix.ToString(), *StepKind.ToString()));
	}
}

USimAg_ChainedJobStrategy::USimAg_ChainedJobStrategy()
{
	DebugName = TEXT("ChainedJob");
}

float USimAg_ChainedJobStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	// No chain assigned => behave exactly like the base single-job strategy.
	if (!ChainTag.IsValid())
	{
		return Super::ScoreFor_Implementation(Context);
	}

	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return 0.f;
	}

	// Resolve the chain asset by tag (synchronous load is cached by the registry).
	USimAg_JobChainAsset* Chain = nullptr;
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(Owner))
	{
		Chain = Registry->Find<USimAg_JobChainAsset>(ChainTag);
	}
	if (!Chain)
	{
		return 0.f;
	}

	// Rebuild the completed-kinds set from the per-kind blackboard bools.
	FGameplayTagContainer Completed;
	if (Context.Blackboard)
	{
		for (const FSimAg_JobStep& Step : Chain->Steps)
		{
			if (Step.JobKind.IsValid() && Context.Blackboard->GetBool(MakeCompletedKey(CompletedKindsPrefix, Step.JobKind), false))
			{
				Completed.AddTag(Step.JobKind);
			}
		}
	}

	const FSimAg_JobStep Next = Chain->GetNextEligibleStep(Completed);
	if (!Next.JobKind.IsValid())
	{
		return 0.f; // chain finished or blocked
	}

	// Capability gate: skip a step the agent isn't skilled for (so the chain stalls cleanly, not wrongly).
	if (Next.RequiredSkill.IsValid())
	{
		const USimAg_SkillComponent* Skills = Owner->FindComponentByClass<USimAg_SkillComponent>();
		if (!Skills || !Skills->HasSkill(Next.RequiredSkill))
		{
			return 0.f;
		}
	}

	// Verify an actual open posting of this kind exists (side-effect-free) before committing a score.
	const TScriptInterface<ISimAg_JobProvider> Provider = SimAg_StrategySupport::ResolveJobProvider(Owner);
	if (!Provider)
	{
		return 0.f;
	}
	const FVector AgentLoc = Owner->GetActorLocation();
	const FSimAg_JobHandle Best = ISimAg_JobProvider::Execute_QueryBestJobFor(Provider.GetObject(), Next.JobKind, AgentLoc);
	if (!Best.IsValid())
	{
		return 0.f;
	}

	// Base priority + a need-coupled boost: if PriorityNeed is set, multiply by its urgency (1 - satisfaction)
	// is read by the base via curves; here we keep it simple and proportional to the step's BasePriority
	// scaled by distance desirability from the base curve.
	const float Dist = static_cast<float>(FVector::Dist(AgentLoc, Best.WorldLocation));
	const FRichCurve* Curve = DesirabilityCurve.GetRichCurveConst();
	const float Desirability = Curve ? Curve->Eval(Dist) : 1.f;
	return FMath::Max(0.f, Next.BasePriority * Desirability);
}

void USimAg_ChainedJobStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	if (!ChainTag.IsValid())
	{
		Super::Execute_Implementation(Context);
		return;
	}

	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	USimAg_JobChainAsset* Chain = nullptr;
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(Owner))
	{
		Chain = Registry->Find<USimAg_JobChainAsset>(ChainTag);
	}
	if (!Chain)
	{
		return;
	}

	FGameplayTagContainer Completed;
	if (Context.Blackboard)
	{
		for (const FSimAg_JobStep& Step : Chain->Steps)
		{
			if (Step.JobKind.IsValid() && Context.Blackboard->GetBool(MakeCompletedKey(CompletedKindsPrefix, Step.JobKind), false))
			{
				Completed.AddTag(Step.JobKind);
			}
		}
	}

	const FSimAg_JobStep Next = Chain->GetNextEligibleStep(Completed);
	if (!Next.JobKind.IsValid())
	{
		return;
	}

	const TScriptInterface<ISimAg_JobProvider> Provider = SimAg_StrategySupport::ResolveJobProvider(Owner);
	if (!Provider)
	{
		return;
	}
	const FVector AgentLoc = Owner->GetActorLocation();
	USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>();

	FSimAg_JobHandle Claimed;
	if (USimAg_JobBoardSubsystem* Board = Cast<USimAg_JobBoardSubsystem>(Provider.GetObject()))
	{
		const FSeam_EntityId AgentId = Agent ? Agent->GetAgentId() : FSeam_EntityId::Invalid();
		Claimed = Board->ClaimJobForAgent(Next.JobKind, AgentLoc, AgentId);
	}
	else
	{
		Claimed = ISimAg_JobProvider::Execute_ClaimJob(Provider.GetObject(), Next.JobKind, AgentLoc);
	}
	if (!Claimed.IsValid())
	{
		return; // lost the race; re-evaluate next pass
	}

	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, Claimed.WorldLocation);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
		// Mark this step's kind completed for the next pass (the agent will travel + work it this cycle).
		Context.Blackboard->SetBool(MakeCompletedKey(CompletedKindsPrefix, Next.JobKind), true);
	}
	if (Agent)
	{
		Agent->SetClaimedJob(Claimed);
		if (WorkActivity.IsValid())
		{
			Agent->SetCurrentActivity(WorkActivity);
		}
	}
}
