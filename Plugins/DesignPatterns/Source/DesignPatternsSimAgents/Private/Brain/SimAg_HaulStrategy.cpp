// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_HaulStrategy.h"
#include "Brain/SimAg_StrategySupport.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "Memory/SimAg_MemoryComponent.h"
#include "FSM/DPBlackboard.h"
#include "GameFramework/Actor.h"

USimAg_HaulStrategy::USimAg_HaulStrategy()
{
	DebugName = TEXT("Haul");
	// Default desirability: falls from 1 at distance 0 to 0 at a large distance (nearer deliveries win).
	if (FRichCurve* Curve = DesirabilityCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(0.f, 1.f);
		Curve->AddKey(5000.f, 0.f);
	}
}

float USimAg_HaulStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !HaulJobKind.IsValid())
	{
		return 0.f;
	}

	// If already mid-haul, keep going (high, stable score) so the agent finishes before re-deciding.
	if (Context.Blackboard)
	{
		const int32 Phase = Context.Blackboard->GetInt(PhaseKey, static_cast<int32>(ESimAg_HaulPhase::Idle));
		if (Phase != static_cast<int32>(ESimAg_HaulPhase::Idle))
		{
			return 1.f;
		}
	}

	const TScriptInterface<ISimAg_JobProvider> Provider = SimAg_StrategySupport::ResolveJobProvider(Owner);
	if (!Provider)
	{
		return 0.f;
	}

	// SIDE-EFFECT-FREE: find an open haul job without claiming it.
	const FVector AgentLoc = Owner->GetActorLocation();
	const FSimAg_JobHandle Best =
		ISimAg_JobProvider::Execute_QueryBestJobFor(Provider.GetObject(), HaulJobKind, AgentLoc);
	if (!Best.IsValid())
	{
		return 0.f;
	}

	// Score by destination distance through the curve.
	const float Dist = static_cast<float>(FVector::Dist(AgentLoc, Best.WorldLocation));
	const FRichCurve* Curve = DesirabilityCurve.GetRichCurveConst();
	return FMath::Max(0.f, Curve ? Curve->Eval(Dist) : 1.f);
}

void USimAg_HaulStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !Context.Blackboard)
	{
		return;
	}

	const FVector AgentLoc = Owner->GetActorLocation();
	USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>();
	const USimAg_MemoryComponent* Memory = Owner->FindComponentByClass<USimAg_MemoryComponent>();

	int32 Phase = Context.Blackboard->GetInt(PhaseKey, static_cast<int32>(ESimAg_HaulPhase::Idle));

	if (Phase == static_cast<int32>(ESimAg_HaulPhase::Idle))
	{
		// Begin a haul: claim the destination job and reserve the source so two agents don't both fetch it.
		const TScriptInterface<ISimAg_JobProvider> Provider = SimAg_StrategySupport::ResolveJobProvider(Owner);
		if (!Provider)
		{
			return;
		}

		FSimAg_JobHandle Claimed;
		if (USimAg_JobBoardSubsystem* Board = Cast<USimAg_JobBoardSubsystem>(Provider.GetObject()))
		{
			const FSeam_EntityId AgentId = Agent ? Agent->GetAgentId() : FSeam_EntityId::Invalid();
			Claimed = Board->ClaimJobForAgent(HaulJobKind, AgentLoc, AgentId);
		}
		else
		{
			Claimed = ISimAg_JobProvider::Execute_ClaimJob(Provider.GetObject(), HaulJobKind, AgentLoc);
		}
		if (!Claimed.IsValid())
		{
			return; // lost the race; re-evaluate next pass
		}

		// Reserve the source so only one agent fetches it. The source is identified by the remembered
		// stockpile fact's Entity; if no entity is remembered we skip reservation (still allowed to haul).
		FVector SourceLoc = AgentLoc;
		if (Memory)
		{
			FSimAg_MemoryFact SourceFact;
			if (Memory->QueryNearest(SourceMemoryKind, AgentLoc, SourceFact))
			{
				SourceLoc = SourceFact.WorldLocation;
				if (SourceFact.Entity.IsValid() && Agent)
				{
					const TScriptInterface<ISeam_JobReservation> Reservation = SimAg_StrategySupport::ResolveReservation(Owner);
					if (Reservation)
					{
						if (!ISeam_JobReservation::Execute_TryReserve(Reservation.GetObject(), SourceFact.Entity, Agent->GetAgentId()))
						{
							// Source already reserved by someone else: abandon this haul attempt cleanly.
							ISimAg_JobProvider::Execute_CompleteJob(Provider.GetObject(), Claimed.JobId);
							return;
						}
					}
				}
			}
		}

		// Stash the delivery destination (the claimed job's location) for the deliver phase, then move to
		// the source first.
		if (Agent)
		{
			Agent->SetClaimedJob(Claimed);
			if (ActivityTag.IsValid())
			{
				Agent->SetCurrentActivity(ActivityTag);
			}
		}
		Context.Blackboard->SetVector(DeliverTargetKey, Claimed.WorldLocation);
		Context.Blackboard->SetVector(MoveTargetKey, SourceLoc);
		Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
		Context.Blackboard->SetInt(PhaseKey, static_cast<int32>(ESimAg_HaulPhase::Fetch));
		return;
	}

	if (Phase == static_cast<int32>(ESimAg_HaulPhase::Fetch))
	{
		// Once at the source, switch to delivering toward the claimed job's location (stashed at claim).
		const FVector Source = Context.Blackboard->GetVector(MoveTargetKey);
		if (FVector::Dist(AgentLoc, Source) <= PhaseArrivalRadius)
		{
			const FVector Destination = Context.Blackboard->GetVector(DeliverTargetKey);
			Context.Blackboard->SetVector(MoveTargetKey, Destination);
			Context.Blackboard->SetInt(PhaseKey, static_cast<int32>(ESimAg_HaulPhase::Deliver));
		}
		return;
	}

	if (Phase == static_cast<int32>(ESimAg_HaulPhase::Deliver))
	{
		const FVector Destination = Context.Blackboard->GetVector(MoveTargetKey);
		if (FVector::Dist(AgentLoc, Destination) <= PhaseArrivalRadius)
		{
			// Delivered: complete the job, release the source reservation, and reset the phase.
			if (Agent)
			{
				const FGuid ClaimedId = Agent->GetClaimedJobId();
				if (ClaimedId.IsValid())
				{
					if (const TScriptInterface<ISimAg_JobProvider> Provider = SimAg_StrategySupport::ResolveJobProvider(Owner))
					{
						ISimAg_JobProvider::Execute_CompleteJob(Provider.GetObject(), ClaimedId);
					}
					Agent->SetClaimedJob(FSimAg_JobHandle::Invalid());
				}
			}
			Context.Blackboard->SetInt(PhaseKey, static_cast<int32>(ESimAg_HaulPhase::Idle));
			Context.Blackboard->SetBool(SimAg_BrainKeys::IsMoving, false);
		}
		return;
	}
}
