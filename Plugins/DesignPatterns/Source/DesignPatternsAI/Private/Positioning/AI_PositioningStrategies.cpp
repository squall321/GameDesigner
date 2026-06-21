// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Positioning/AI_PositioningStrategies.h"
#include "Query/AI_QuerySubsystem.h"
#include "Query/AI_EnvQuery.h"
#include "Seams/AI_Squad.h"
#include "AI/Seam_CoverProvider.h"
#include "Identity/Seam_EntityIdentity.h"
#include "DesignPatternsAINativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

//~ Base ----------------------------------------------------------------------------------------

float UAI_PositioningStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	// Applicable when we have an owner and a threat to position relative to.
	if (!Context.Owner.IsValid())
	{
		return 0.f;
	}
	const FVector Threat = ReadThreatLocation(Context);
	return Threat.IsZero() ? 0.f : 1.f;
}

void UAI_PositioningStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
{
	const FVector OwnerLoc = ResolveOwnerLocation(Context);
	const FVector Threat = ReadThreatLocation(Context);

	FVector Destination;
	if (!ComputeDestination(Context, OwnerLoc, Threat, Destination))
	{
		return;
	}

	// Optionally refine through an EQS-style query centered on the raw destination.
	Destination = RefineWithQuery(Context, Destination, Threat);

	if (Context.Blackboard.GetObject())
	{
		Context.Blackboard->SetVector(BlackboardKey_MoveTarget, Destination);
	}
}

bool UAI_PositioningStrategy::ComputeDestination(const FDP_StrategyContext& /*Context*/, const FVector& OwnerLocation,
	const FVector& /*ThreatLocation*/, FVector& OutDestination) const
{
	// Base: stay put (subclasses override). Returning the owner location is a safe no-op move.
	OutDestination = OwnerLocation;
	return true;
}

FVector UAI_PositioningStrategy::ReadThreatLocation(const FDP_StrategyContext& Context) const
{
	if (Context.Blackboard.GetObject())
	{
		return Context.Blackboard->GetVector(BlackboardKey_ThreatLocation, FVector::ZeroVector);
	}
	return FVector::ZeroVector;
}

FVector UAI_PositioningStrategy::ResolveOwnerLocation(const FDP_StrategyContext& Context) const
{
	const AActor* Owner = Context.Owner.Get();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

FVector UAI_PositioningStrategy::RefineWithQuery(const FDP_StrategyContext& Context, const FVector& RawDestination, const FVector& ThreatLocation) const
{
	if (!PlacementQuery)
	{
		return RawDestination;
	}
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return RawDestination;
	}
	UAI_QuerySubsystem* QuerySys = FDP_SubsystemStatics::GetWorldSubsystem<UAI_QuerySubsystem>(Owner);
	if (!QuerySys)
	{
		return RawDestination;
	}

	FAI_QueryContext QueryContext;
	QueryContext.Querier = Owner;
	QueryContext.QuerierLocation = RawDestination; // generate candidates around the raw target
	QueryContext.TargetLocation = ThreatLocation;

	FAI_ScoredPoint Best;
	if (QuerySys->RunQueryBest(PlacementQuery, QueryContext, Best))
	{
		return Best.Location;
	}
	return RawDestination;
}

//~ Flank ---------------------------------------------------------------------------------------

bool UAI_FlankStrategy::ComputeDestination(const FDP_StrategyContext& /*Context*/, const FVector& OwnerLocation,
	const FVector& ThreatLocation, FVector& OutDestination) const
{
	FVector ToThreat = (ThreatLocation - OwnerLocation);
	ToThreat.Z = 0.f;
	if (ToThreat.IsNearlyZero())
	{
		return false;
	}
	const FVector ApproachDir = ToThreat.GetSafeNormal();

	// Rotate the approach by the flank angle (choose the side that is currently to our right).
	const float SignedAngle = FlankAngleDegrees;
	const FVector Rotated = ApproachDir.RotateAngleAxis(SignedAngle, FVector::UpVector);

	OutDestination = ThreatLocation - Rotated * FMath::Max(1.f, PreferredRange);
	OutDestination.Z = OwnerLocation.Z;
	return true;
}

//~ Surround ------------------------------------------------------------------------------------

bool UAI_SurroundStrategy::ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
	const FVector& ThreatLocation, FVector& OutDestination) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner)
	{
		return false;
	}

	// Resolve the squad seam + this agent's index among the members so the ring distributes evenly.
	int32 MyIndex = 0;
	int32 Count = 1;
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Owner))
	{
		if (UObject* SquadObj = Locator->ResolveService(AINativeTags::Service_AI_Squad))
		{
			if (SquadObj->GetClass()->ImplementsInterface(UAI_Squad::StaticClass()))
			{
				if (IAI_Squad* Squad = Cast<IAI_Squad>(SquadObj))
				{
					TArray<FSeam_EntityId> Members;
					Squad->GetMembers(Members);
					if (Members.Num() > 0)
					{
						Count = Members.Num();
						// Find our index by matching the owner's entity id; default 0 if not found.
						if (Owner->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
						{
							const FSeam_EntityId Me = ISeam_EntityIdentity::Execute_GetEntityId(Owner);
							const int32 Found = Members.IndexOfByPredicate([&Me](const FSeam_EntityId& M) { return M == Me; });
							MyIndex = (Found != INDEX_NONE) ? Found : 0;
						}
					}
				}
			}
		}
	}

	const float Angle = (2.f * PI) * (static_cast<float>(MyIndex) / static_cast<float>(FMath::Max(1, Count)));
	const FVector Offset(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
	OutDestination = ThreatLocation + Offset * FMath::Max(1.f, PreferredRange);
	OutDestination.Z = OwnerLocation.Z;
	return true;
}

//~ Kite ----------------------------------------------------------------------------------------

float UAI_KiteStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	if (!Context.Owner.IsValid())
	{
		return 0.f;
	}
	const FVector Threat = ReadThreatLocation(Context);
	if (Threat.IsZero())
	{
		return 0.f;
	}
	const FVector OwnerLoc = ResolveOwnerLocation(Context);
	const float Dist = FVector::Dist(OwnerLoc, Threat);
	// Only kite when the threat is uncomfortably close; score scales with how close it is.
	if (Dist >= MinThreatDistance)
	{
		return 0.f;
	}
	return 1.f + (MinThreatDistance - Dist) / FMath::Max(1.f, MinThreatDistance);
}

bool UAI_KiteStrategy::ComputeDestination(const FDP_StrategyContext& /*Context*/, const FVector& OwnerLocation,
	const FVector& ThreatLocation, FVector& OutDestination) const
{
	FVector AwayDir = (OwnerLocation - ThreatLocation);
	AwayDir.Z = 0.f;
	if (AwayDir.IsNearlyZero())
	{
		AwayDir = FVector::ForwardVector;
	}
	AwayDir = AwayDir.GetSafeNormal();
	OutDestination = ThreatLocation + AwayDir * FMath::Max(1.f, PreferredRange);
	OutDestination.Z = OwnerLocation.Z;
	return true;
}

//~ CoverAdvance --------------------------------------------------------------------------------

bool UAI_CoverAdvanceStrategy::ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
	const FVector& ThreatLocation, FVector& OutDestination) const
{
	AActor* Owner = Context.Owner.Get();
	if (Owner)
	{
		// Prefer cover from the cover provider seam.
		if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Owner))
		{
			if (UObject* CoverObj = Locator->ResolveService(AINativeTags::Service_AI_Cover))
			{
				if (CoverObj->GetClass()->ImplementsInterface(USeam_CoverProvider::StaticClass()))
				{
					if (ISeam_CoverProvider* Cover = Cast<ISeam_CoverProvider>(CoverObj))
					{
						FTransform CoverXform;
						FSeam_EntityId CoverId;
						if (Cover->FindBestCover(OwnerLocation, ThreatLocation, CoverSearchRadius, CoverXform, CoverId))
						{
							OutDestination = CoverXform.GetLocation();
							return true;
						}
					}
				}
			}
		}
	}

	// Fallback: direct advance toward the threat to PreferredRange.
	FVector ToThreat = (ThreatLocation - OwnerLocation);
	ToThreat.Z = 0.f;
	if (ToThreat.IsNearlyZero())
	{
		return false;
	}
	const FVector Dir = ToThreat.GetSafeNormal();
	OutDestination = ThreatLocation - Dir * FMath::Max(1.f, PreferredRange);
	OutDestination.Z = OwnerLocation.Z;
	return true;
}
