// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_SocializeStrategy.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Memory/SimAg_MemoryComponent.h"
#include "Social/SimAg_SocialComponent.h"
#include "Needs/Seam_NeedProvider.h"
#include "FSM/DPBlackboard.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Resolve an ISeam_NeedProvider that supports NeedTag off Actor. Invalid if none answers it. */
	TScriptInterface<ISeam_NeedProvider> ResolveNeedProvider(AActor* Actor, const FGameplayTag& NeedTag)
	{
		if (!Actor || !NeedTag.IsValid())
		{
			return TScriptInterface<ISeam_NeedProvider>();
		}
		auto Consider = [&NeedTag](UObject* Candidate) -> TScriptInterface<ISeam_NeedProvider>
		{
			if (Candidate && Candidate->Implements<USeam_NeedProvider>()
				&& ISeam_NeedProvider::Execute_SupportsNeed(Candidate, NeedTag))
			{
				TScriptInterface<ISeam_NeedProvider> Result;
				Result.SetObject(Candidate);
				Result.SetInterface(Cast<ISeam_NeedProvider>(Candidate));
				return Result;
			}
			return TScriptInterface<ISeam_NeedProvider>();
		};
		if (TScriptInterface<ISeam_NeedProvider> FromActor = Consider(Actor))
		{
			return FromActor;
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (TScriptInterface<ISeam_NeedProvider> FromComp = Consider(Comp))
			{
				return FromComp;
			}
		}
		return TScriptInterface<ISeam_NeedProvider>();
	}

	/**
	 * Pick the best-liked remembered agent: walk the memory facts of AgentMemoryKind, look up each
	 * remembered entity's affinity, and return the one with the highest affinity (>= MinAffinity) plus its
	 * remembered location. Returns false when none qualifies.
	 */
	bool FindBestPartner(
		const USimAg_MemoryComponent& Memory, const USimAg_SocialComponent* Social,
		const FGameplayTag& AgentMemoryKind, float MinAffinity, FVector& OutLocation, float& OutAffinity)
	{
		bool bFound = false;
		float BestAffinity = MinAffinity;
		for (const FSimAg_MemoryFact& Fact : Memory.GetFacts())
		{
			if (!Fact.Subject.MatchesTag(AgentMemoryKind) || !Fact.Entity.IsValid())
			{
				continue;
			}
			const float Affinity = Social ? Social->GetAffinity(Fact.Entity) : 0.f;
			if (Affinity >= BestAffinity)
			{
				BestAffinity = Affinity;
				OutLocation = Fact.WorldLocation;
				OutAffinity = Affinity;
				bFound = true;
			}
		}
		return bFound;
	}
}

USimAg_SocializeStrategy::USimAg_SocializeStrategy()
{
	DebugName = TEXT("Socialize");
	if (FRichCurve* Curve = UrgencyCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(0.f, 0.f);
		Curve->AddKey(1.f, 1.f);
	}
	// Default affinity curve: maps [-1,1] affinity to [0,1] multiplier (disliked partners suppressed).
	if (FRichCurve* Curve = AffinityCurve.GetRichCurve())
	{
		Curve->Reset();
		Curve->AddKey(-1.f, 0.f);
		Curve->AddKey(1.f, 1.f);
	}
}

float USimAg_SocializeStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	AActor* Owner = Context.Owner.Get();
	if (!Owner || !SocialNeedTag.IsValid() || !AgentMemoryKind.IsValid())
	{
		return 0.f;
	}

	const USimAg_MemoryComponent* Memory = Owner->FindComponentByClass<USimAg_MemoryComponent>();
	if (!Memory)
	{
		return 0.f;
	}
	const USimAg_SocialComponent* Social = Owner->FindComponentByClass<USimAg_SocialComponent>();

	// Need urgency.
	const TScriptInterface<ISeam_NeedProvider> Provider = ResolveNeedProvider(Owner, SocialNeedTag);
	if (!Provider)
	{
		return 0.f;
	}
	const float Satisfaction = FMath::Clamp(
		ISeam_NeedProvider::Execute_GetNeedNormalized(Provider.GetObject(), SocialNeedTag), 0.f, 1.f);
	const float Urgency = 1.f - Satisfaction;

	// Best available partner.
	FVector PartnerLoc;
	float PartnerAffinity = 0.f;
	if (!FindBestPartner(*Memory, Social, AgentMemoryKind, MinPartnerAffinity, PartnerLoc, PartnerAffinity))
	{
		return 0.f; // no liked partner known
	}

	const FRichCurve* UrgencyEval = UrgencyCurve.GetRichCurveConst();
	const float UrgencyScore = UrgencyEval ? UrgencyEval->Eval(Urgency) : Urgency;

	const FRichCurve* AffinityEval = AffinityCurve.GetRichCurveConst();
	const float AffinityMul = AffinityEval ? AffinityEval->Eval(PartnerAffinity) : FMath::Clamp((PartnerAffinity + 1.f) * 0.5f, 0.f, 1.f);

	return FMath::Max(0.f, UrgencyScore * AffinityMul);
}

void USimAg_SocializeStrategy::Execute_Implementation(const FDP_StrategyContext& Context)
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
	const USimAg_SocialComponent* Social = Owner->FindComponentByClass<USimAg_SocialComponent>();

	FVector PartnerLoc;
	float PartnerAffinity = 0.f;
	if (!FindBestPartner(*Memory, Social, AgentMemoryKind, MinPartnerAffinity, PartnerLoc, PartnerAffinity))
	{
		return;
	}

	if (Context.Blackboard)
	{
		Context.Blackboard->SetVector(MoveTargetKey, PartnerLoc);
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
