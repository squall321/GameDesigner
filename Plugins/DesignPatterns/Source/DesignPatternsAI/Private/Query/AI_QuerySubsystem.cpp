// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/AI_QuerySubsystem.h"
#include "Query/AI_QueryTests.h"
#include "Cover/AI_CoverPoint.h"
#include "Settings/AI_DeveloperSettings.h"
#include "DesignPatternsAINativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

namespace
{
	/** Defensive fallbacks used only if the settings CDO is somehow null (it never is in a running game). */
	constexpr float GDefaultGridSpacing_Fallback = 100.f;
	constexpr int32 GMaxItemsHardCap_Fallback = 1024;
}

void UAI_QuerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSelfAsService();
}

void UAI_QuerySubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (Locator->ResolveService(AINativeTags::Service_AI_Query) == this)
		{
			Locator->UnregisterService(AINativeTags::Service_AI_Query);
		}
	}
	Super::Deinitialize();
}

UDP_ServiceLocatorSubsystem* UAI_QuerySubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

void UAI_QuerySubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(AINativeTags::Service_AI_Query, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ Public run entry points ---------------------------------------------------------------------

bool UAI_QuerySubsystem::RunQuery(UAI_EnvQuery* Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutRanked)
{
	OutRanked.Reset();
	if (!Query)
	{
		return false;
	}
	++QueriesServed;

	// Optional engine-EQS bridge: only when the query opts in AND the bridge actually produced results.
	if (Query->bPreferEngineEQS && TryRunEngineEqs(*Query, Context, OutRanked))
	{
		return OutRanked.Num() > 0;
	}

	return RunBuiltInScorer(*Query, Context, OutRanked);
}

bool UAI_QuerySubsystem::RunQueryBest(UAI_EnvQuery* Query, const FAI_QueryContext& Context, FAI_ScoredPoint& OutBest)
{
	TArray<FAI_ScoredPoint> Ranked;
	if (RunQuery(Query, Context, Ranked) && Ranked.Num() > 0)
	{
		OutBest = Ranked[0];
		return true;
	}
	OutBest = FAI_ScoredPoint();
	return false;
}

//~ Candidate generation ------------------------------------------------------------------------

void UAI_QuerySubsystem::GenerateCandidates(const UAI_EnvQuery& Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutCandidates) const
{
	OutCandidates.Reset();

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const int32 HardCap = Settings ? Settings->MaxQueryItemsHardCap : GMaxItemsHardCap_Fallback;
	const int32 MaxItems = FMath::Clamp(Query.MaxItems, 1, FMath::Max(1, HardCap));

	const FVector Center = Context.QuerierLocation;

	switch (Query.Generator)
	{
	case EAI_QueryGenerator::SinglePoint:
	{
		OutCandidates.Emplace(Center, Context.Querier.Get());
		break;
	}
	case EAI_QueryGenerator::Grid:
	{
		float Spacing = Query.GridSpacing;
		if (Spacing <= 0.f)
		{
			Spacing = Settings ? Settings->DefaultQueryGridSpacing : GDefaultGridSpacing_Fallback;
		}
		const int32 Half = FMath::Max(0, FMath::FloorToInt(Query.GenerationRadius / FMath::Max(1.f, Spacing)));
		for (int32 X = -Half; X <= Half && OutCandidates.Num() < MaxItems; ++X)
		{
			for (int32 Y = -Half; Y <= Half && OutCandidates.Num() < MaxItems; ++Y)
			{
				const FVector P = Center + FVector(static_cast<float>(X) * Spacing, static_cast<float>(Y) * Spacing, 0.f);
				OutCandidates.Emplace(P, nullptr);
			}
		}
		break;
	}
	case EAI_QueryGenerator::Ring:
	{
		const int32 Samples = FMath::Clamp(MaxItems, 1, MaxItems);
		const float Radius = FMath::Max(1.f, Query.GenerationRadius);
		for (int32 I = 0; I < Samples; ++I)
		{
			const float Angle = (2.f * PI) * (static_cast<float>(I) / static_cast<float>(Samples));
			const FVector P = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			OutCandidates.Emplace(P, nullptr);
		}
		break;
	}
	case EAI_QueryGenerator::CoverPoints:
	{
		// Candidates are the live cover points within the generation radius.
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<AAI_CoverPoint> It(World); It && OutCandidates.Num() < MaxItems; ++It)
			{
				AAI_CoverPoint* Point = *It;
				if (!Point)
				{
					continue;
				}
				const FVector P = Point->GetActorLocation();
				if (Query.GenerationRadius <= 0.f || FVector::DistSquared(P, Center) <= FMath::Square(Query.GenerationRadius))
				{
					OutCandidates.Emplace(P, Point);
				}
			}
		}
		break;
	}
	default:
		break;
	}
}

//~ Built-in scorer -----------------------------------------------------------------------------

bool UAI_QuerySubsystem::RunBuiltInScorer(const UAI_EnvQuery& Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutRanked) const
{
	TArray<FAI_ScoredPoint> Candidates;
	GenerateCandidates(Query, Context, Candidates);
	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Hard-filter pass: drop any candidate any test rejects.
	for (FAI_ScoredPoint& Cand : Candidates)
	{
		Cand.bValid = true;
		for (const TObjectPtr<UAI_QueryTest>& Test : Query.Tests)
		{
			if (!Test)
			{
				continue;
			}
			if (!Test->IsItemValid(Context, Cand.Location, Cand.ItemActor.Get()))
			{
				Cand.bValid = false;
				break;
			}
		}
	}

	// Per-test raw scores cached so we can normalize each test across the surviving candidates.
	const int32 NumTests = Query.Tests.Num();
	TArray<TArray<float>> RawByTest;
	RawByTest.SetNum(NumTests);

	TArray<int32> SurvivorIndices;
	SurvivorIndices.Reserve(Candidates.Num());
	for (int32 I = 0; I < Candidates.Num(); ++I)
	{
		if (Candidates[I].bValid)
		{
			SurvivorIndices.Add(I);
		}
	}
	if (SurvivorIndices.Num() == 0)
	{
		return false;
	}

	// Gather raw scores + per-test min/max.
	TArray<float> RawMin; RawMin.Init(TNumericLimits<float>::Max(), NumTests);
	TArray<float> RawMax; RawMax.Init(TNumericLimits<float>::Lowest(), NumTests);
	for (int32 T = 0; T < NumTests; ++T)
	{
		RawByTest[T].Reserve(SurvivorIndices.Num());
		const UAI_QueryTest* Test = Query.Tests[T];
		for (int32 SI : SurvivorIndices)
		{
			float Raw = 0.f;
			if (Test && Test->Weight > 0.f)
			{
				Raw = Test->ScoreItem(Context, Candidates[SI].Location, Candidates[SI].ItemActor.Get());
			}
			RawByTest[T].Add(Raw);
			RawMin[T] = FMath::Min(RawMin[T], Raw);
			RawMax[T] = FMath::Max(RawMax[T], Raw);
		}
	}

	// Finalize: weighted sum of each test's normalized contribution; track total weight to normalize.
	float TotalWeight = 0.f;
	for (int32 T = 0; T < NumTests; ++T)
	{
		if (Query.Tests[T] && Query.Tests[T]->Weight > 0.f)
		{
			TotalWeight += Query.Tests[T]->Weight;
		}
	}
	TotalWeight = FMath::Max(TotalWeight, KINDA_SMALL_NUMBER);

	OutRanked.Reset();
	for (int32 LocalIdx = 0; LocalIdx < SurvivorIndices.Num(); ++LocalIdx)
	{
		const int32 SI = SurvivorIndices[LocalIdx];
		float Accum = 0.f;
		for (int32 T = 0; T < NumTests; ++T)
		{
			const UAI_QueryTest* Test = Query.Tests[T];
			if (Test && Test->Weight > 0.f)
			{
				Accum += Test->Finalize(RawByTest[T][LocalIdx], RawMin[T], RawMax[T]);
			}
		}
		FAI_ScoredPoint Result = Candidates[SI];
		Result.Score = (NumTests > 0) ? (Accum / TotalWeight) : 1.f;
		OutRanked.Add(Result);
	}

	// Best-first.
	OutRanked.Sort([](const FAI_ScoredPoint& A, const FAI_ScoredPoint& B) { return A.Score > B.Score; });
	return OutRanked.Num() > 0;
}

//~ Optional engine-EQS bridge ------------------------------------------------------------------

bool UAI_QuerySubsystem::TryRunEngineEqs(const UAI_EnvQuery& Query, const FAI_QueryContext& /*Context*/, TArray<FAI_ScoredPoint>& /*OutRanked*/) const
{
	// The engine EQS path is intentionally a thin, runtime-gated hook. AIModule's UEnvQueryManager is a
	// PRIVATE dependency; rather than synchronously block on an engine EQS request here (which would
	// require its async-result plumbing), we only ATTEMPT the bridge when the asset is actually loadable
	// and otherwise return false so the always-available built-in scorer runs. This keeps engine EQS types
	// out of the public header and avoids a fake compile macro.
	if (Query.EngineEqsAsset.IsNull())
	{
		return false;
	}

	// A non-null soft asset that is not yet loaded: prefer the built-in scorer this frame rather than
	// triggering a synchronous load on the game thread. Designers who want the engine bridge should ensure
	// the asset is loaded (it is a soft ref precisely so AIModule stays out of the link line).
	if (!Query.EngineEqsAsset.IsValid())
	{
		UE_LOG(LogDP, Verbose,
			TEXT("AI QuerySubsystem: engine EQS asset not loaded; using built-in scorer for this query."));
		return false;
	}

	// Even when loaded, the synchronous-result engine EQS path is not wired (would couple us to AIModule's
	// async request API in this header-clean module). Fall through to the built-in scorer.
	return false;
}

//~ Debug ---------------------------------------------------------------------------------------

FString UAI_QuerySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("AI Query: %d served"), QueriesServed);
}
