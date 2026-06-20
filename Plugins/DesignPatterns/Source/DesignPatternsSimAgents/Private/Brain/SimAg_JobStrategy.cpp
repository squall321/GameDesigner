// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_JobStrategy.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Jobs/SimAg_JobProvider.h"
#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * Resolve the ISimAg_JobProvider seam: first off the owning actor (so a per-squad board on the actor
	 * wins), then via the service locator under Service_JobBoard (the world board). Returns an invalid
	 * interface if neither is present.
	 */
	TScriptInterface<ISimAg_JobProvider> ResolveJobProvider(AActor* Actor)
	{
		auto Consider = [](UObject* Candidate) -> TScriptInterface<ISimAg_JobProvider>
		{
			if (Candidate && Candidate->Implements<USimAg_JobProvider>())
			{
				TScriptInterface<ISimAg_JobProvider> Result;
				Result.SetObject(Candidate);
				Result.SetInterface(Cast<ISimAg_JobProvider>(Candidate));
				return Result;
			}
			return TScriptInterface<ISimAg_JobProvider>();
		};

		if (Actor)
		{
			if (TScriptInterface<ISimAg_JobProvider> FromActor = Consider(Actor))
			{
				return FromActor;
			}
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (TScriptInterface<ISimAg_JobProvider> FromComp = Consider(Comp))
				{
					return FromComp;
				}
			}

			// World board via the service locator (resolved through the locator so we never hard-depend on
			// the concrete subsystem here beyond the seam type).
			if (UDP_ServiceLocatorSubsystem* Locator =
				FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Actor))
			{
				if (UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_JobBoard))
				{
					if (TScriptInterface<ISimAg_JobProvider> FromService = Consider(Service))
					{
						return FromService;
					}
				}
			}
		}
		return TScriptInterface<ISimAg_JobProvider>();
	}
}

USimAg_JobStrategy::USimAg_JobStrategy()
{
	DebugName = TEXT("Job");
	// Default desirability curve: falls from 1 at distance 0 to 0 at a large distance, so nearer jobs are
	// preferred. Designers reshape; the two endpoints are an honest default, not a hidden weight.
	if (FRichCurve* Curve = DesirabilityCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(0.f, 1.f);
		Curve->AddKey(5000.f, 0.f);
	}
}

float USimAg_JobStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !JobKind.IsValid())
	{
		return 0.f;
	}

	const TScriptInterface<ISimAg_JobProvider> Provider = ResolveJobProvider(Owner);
	if (!Provider)
	{
		return 0.f;
	}

	// SIDE-EFFECT-FREE: query the best open job WITHOUT claiming it (Execute does the claiming).
	const FVector AgentLoc = Owner->GetActorLocation();
	const FSimAg_JobHandle Best =
		ISimAg_JobProvider::Execute_QueryBestJobFor(Provider.GetObject(), JobKind, AgentLoc);
	if (!Best.IsValid())
	{
		return 0.f;
	}

	const float Dist = static_cast<float>(FVector::Dist(AgentLoc, Best.WorldLocation));
	const FRichCurve* Curve = DesirabilityCurve.GetRichCurveConst();
	const float Desirability = Curve ? Curve->Eval(Dist) : 1.f;
	return FMath::Max(0.f, Desirability);
}

void USimAg_JobStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return;
	}

	const TScriptInterface<ISimAg_JobProvider> Provider = ResolveJobProvider(Owner);
	if (!Provider)
	{
		return;
	}

	const FVector AgentLoc = Owner->GetActorLocation();
	USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>();

	// Prefer the identity-bearing claim path on the concrete world board so the posting records WHO
	// claimed it; fall back to the seam's anonymous ClaimJob for any other provider implementer.
	FSimAg_JobHandle Claimed;
	if (USimAg_JobBoardSubsystem* Board = Cast<USimAg_JobBoardSubsystem>(Provider.GetObject()))
	{
		const FSeam_EntityId AgentId = Agent ? Agent->GetAgentId() : FSeam_EntityId::Invalid();
		Claimed = Board->ClaimJobForAgent(JobKind, AgentLoc, AgentId);
	}
	else
	{
		Claimed = ISimAg_JobProvider::Execute_ClaimJob(Provider.GetObject(), JobKind, AgentLoc);
	}

	if (!Claimed.IsValid())
	{
		return; // someone else won the race this pass; the brain will re-evaluate next tick
	}

	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, Claimed.WorldLocation);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
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
