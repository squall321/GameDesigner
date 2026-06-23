// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_MoodWeightedNeedStrategy.h"
#include "Mood/Seam_MoodProvider.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Resolve an ISeam_MoodProvider off Actor (the actor or one of its components). Invalid if none. */
	TScriptInterface<ISeam_MoodProvider> ResolveMoodProvider(AActor* Actor)
	{
		if (!Actor)
		{
			return TScriptInterface<ISeam_MoodProvider>();
		}
		auto Consider = [](UObject* Candidate) -> TScriptInterface<ISeam_MoodProvider>
		{
			if (Candidate && Candidate->Implements<USeam_MoodProvider>())
			{
				TScriptInterface<ISeam_MoodProvider> Result;
				Result.SetObject(Candidate);
				Result.SetInterface(Cast<ISeam_MoodProvider>(Candidate));
				return Result;
			}
			return TScriptInterface<ISeam_MoodProvider>();
		};

		if (TScriptInterface<ISeam_MoodProvider> FromActor = Consider(Actor))
		{
			return FromActor;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (TScriptInterface<ISeam_MoodProvider> FromComp = Consider(Comp))
			{
				return FromComp;
			}
		}
		return TScriptInterface<ISeam_MoodProvider>();
	}
}

USimAg_MoodWeightedNeedStrategy::USimAg_MoodWeightedNeedStrategy()
{
	DebugName = TEXT("MoodWeightedNeed");
}

float USimAg_MoodWeightedNeedStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	// Reuse the shipped need scoring (need resolution + urgency curve) verbatim.
	const float BaseScore = Super::ScoreFor_Implementation(Context);
	if (BaseScore <= 0.f)
	{
		return BaseScore; // not applicable; mood can't make an inapplicable need urgent
	}

	AActor* Owner = Context.Owner.Get();
	const TScriptInterface<ISeam_MoodProvider> Mood = ResolveMoodProvider(Owner);
	if (!Mood)
	{
		return BaseScore; // no mood model => seam's inert 1.0 multiplier => identical to base
	}

	const float Multiplier = FMath::Max(0.f,
		ISeam_MoodProvider::Execute_GetNeedWeightMultiplier(Mood.GetObject(), NeedTag));
	return FMath::Max(0.f, BaseScore * Multiplier);
}
