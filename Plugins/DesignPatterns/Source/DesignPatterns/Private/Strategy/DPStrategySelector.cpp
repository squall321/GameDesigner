// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/DPStrategySelector.h"
#include "Core/DPLog.h"

DECLARE_CYCLE_STAT(TEXT("Strategy Select"), STAT_DP_StrategySelect, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("Strategy Selections"), STAT_DP_StrategySelections, STATGROUP_DesignPatterns);

UDP_Strategy* UDP_StrategySelector::Select(const FDP_StrategyContext& Context) const
{
	SCOPE_CYCLE_COUNTER(STAT_DP_StrategySelect);
	INC_DWORD_STAT(STAT_DP_StrategySelections);

	// Base policy: first applicable strategy.
	for (UDP_Strategy* Strategy : Strategies)
	{
		if (Strategy && Strategy->ScoreFor(Context) > 0.f)
		{
			return Strategy;
		}
	}
	return nullptr;
}

UDP_Strategy* UDP_StrategySelector::SelectAndExecute(const FDP_StrategyContext& Context)
{
	if (UDP_Strategy* Chosen = Select(Context))
	{
		Chosen->Execute(Context);
		return Chosen;
	}
	return nullptr;
}

UDP_Strategy* UDP_HighestScoreSelector::Select(const FDP_StrategyContext& Context) const
{
	SCOPE_CYCLE_COUNTER(STAT_DP_StrategySelect);
	INC_DWORD_STAT(STAT_DP_StrategySelections);

	UDP_Strategy* Best = nullptr;
	float BestScore = ScoreThreshold;

	for (UDP_Strategy* Strategy : Strategies)
	{
		if (!Strategy)
		{
			continue;
		}

		const float Score = Strategy->ScoreFor(Context);
		// Strictly greater so earliest entry wins ties.
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Strategy;
		}
	}

	UE_LOG(LogDPFSM, VeryVerbose, TEXT("HighestScoreSelector picked '%s' (score=%.3f)"),
		Best ? *Best->GetDebugName().ToString() : TEXT("<none>"), BestScore);
	return Best;
}

UDP_Strategy* UDP_PrioritySelector::Select(const FDP_StrategyContext& Context) const
{
	SCOPE_CYCLE_COUNTER(STAT_DP_StrategySelect);
	INC_DWORD_STAT(STAT_DP_StrategySelections);

	// Array order is priority: return the first applicable entry.
	for (UDP_Strategy* Strategy : Strategies)
	{
		if (Strategy && Strategy->ScoreFor(Context) > 0.f)
		{
			UE_LOG(LogDPFSM, VeryVerbose, TEXT("PrioritySelector picked '%s'"),
				*Strategy->GetDebugName().ToString());
			return Strategy;
		}
	}
	return nullptr;
}
