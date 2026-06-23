// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Zone/SimGrid_ZoneRuleStrategy.h"
#include "Zone/SimGrid_ZoneCarrier.h"
#include "World/SimGrid_CoordTypes.h"

float USimGrid_ZoneRuleStrategy::EvaluateGrowth_Implementation(const FSimGrid_ZoneGrowthContext& Context) const
{
	// Inert base: development unchanged.
	return Context.CurrentGrowth;
}

float USimGrid_ClusterGrowthRule::EvaluateGrowth_Implementation(const FSimGrid_ZoneGrowthContext& Context) const
{
	const ASimGrid_ZoneCarrier* Carrier = Context.Carrier;
	if (!Carrier)
	{
		return Context.CurrentGrowth;
	}

	// Count 8-neighbours sharing this cell's zone type.
	TArray<FSeam_CellCoord> Neighbours;
	FSimGrid_CoordMath::GetNeighbours(Context.Cell, ESimGrid_Adjacency::Eight, Neighbours);
	int32 SameZoneCount = 0;
	for (const FSeam_CellCoord& N : Neighbours)
	{
		if (Carrier->GetZoneAt(N) == Context.ZoneTypeTag)
		{
			++SameZoneCount;
		}
	}

	const float Elapsed = FMath::Max(0.f, Context.ElapsedDays);
	float NewGrowth = Context.CurrentGrowth;
	if (SameZoneCount >= RequiredAdjacentMatches)
	{
		NewGrowth += GrowthRatePerDay * Elapsed;
	}
	else
	{
		NewGrowth -= DecayRatePerDay * Elapsed;
	}
	return FMath::Clamp(NewGrowth, 0.f, 1.f);
}
