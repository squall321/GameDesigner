// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/AI_EnvQuery.h"

#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

float UAI_QueryTest::ScoreItem_Implementation(const FAI_QueryContext& /*Context*/, const FVector& /*ItemLocation*/, AActor* /*ItemActor*/) const
{
	// Base test is neutral: a flat score so a test left un-overridden does not bias the result.
	return 1.f;
}

bool UAI_QueryTest::IsItemValid_Implementation(const FAI_QueryContext& /*Context*/, const FVector& /*ItemLocation*/, AActor* /*ItemActor*/) const
{
	// Base test accepts every candidate (no hard filter).
	return true;
}

float UAI_QueryTest::Finalize(float RawScore, float RawMin, float RawMax) const
{
	if (Weight <= 0.f)
	{
		return 0.f;
	}

	// Normalize the raw score across the run's observed [RawMin, RawMax] to [0,1]. A degenerate range
	// (all candidates equal) maps everything to a neutral 0.5 so the test neither favours nor punishes.
	float Normalized = 0.5f;
	const float Range = RawMax - RawMin;
	if (Range > KINDA_SMALL_NUMBER)
	{
		Normalized = FMath::Clamp((RawScore - RawMin) / Range, 0.f, 1.f);
	}

	// Optional designer response remap (linear when the curve has no keys).
	if (ResponseCurve.GetRichCurveConst() && ResponseCurve.GetRichCurveConst()->GetNumKeys() > 0)
	{
		Normalized = FMath::Clamp(ResponseCurve.GetRichCurveConst()->Eval(Normalized), 0.f, 1.f);
	}

	if (bInvert)
	{
		Normalized = 1.f - Normalized;
	}

	return Normalized * Weight;
}

UDP_ServiceLocatorSubsystem* UAI_QueryTest::ResolveLocator(const FAI_QueryContext& Context) const
{
	const AActor* Querier = Context.Querier.Get();
	if (!Querier)
	{
		return nullptr;
	}
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Querier);
}
