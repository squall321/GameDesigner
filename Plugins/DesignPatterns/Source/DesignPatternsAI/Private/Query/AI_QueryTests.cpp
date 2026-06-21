// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/AI_QueryTests.h"

#include "AI/Seam_CoverProvider.h"
#include "Identity/Seam_TeamAffinity.h"
#include "DesignPatternsAINativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

//~ Distance ------------------------------------------------------------------------------------

float UAI_QueryTest_Distance::ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	const FVector Reference = bReferenceIsTarget ? Context.TargetLocation : Context.QuerierLocation;
	const float Dist = FVector::Dist(ItemLocation, Reference);

	// Triangular response peaking at IdealDistance, reaching 0 at MaxDistance (or at 0 below ideal).
	const float Span = FMath::Max(1.f, MaxDistance);
	const float Delta = FMath::Abs(Dist - IdealDistance);
	return FMath::Clamp(1.f - (Delta / Span), 0.f, 1.f);
}

//~ LineOfSight ---------------------------------------------------------------------------------

bool UAI_QueryTest_LineOfSight::HasLineOfSight(const FAI_QueryContext& Context, const FVector& ItemLocation) const
{
	const AActor* Querier = Context.Querier.Get();
	const UWorld* World = Querier ? Querier->GetWorld() : nullptr;
	if (!World)
	{
		return true; // no world to trace against → do not hard-fail candidates
	}

	const FVector Reference = bToTarget ? Context.TargetLocation : Context.QuerierLocation;
	const FVector Eye(0.f, 0.f, EyeHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AI_QueryLoS), /*bTraceComplex=*/false);
	if (Querier)
	{
		Params.AddIgnoredActor(Querier);
	}

	// LoS is clear when nothing blocks between the candidate eye and the reference eye.
	return !World->LineTraceTestByChannel(ItemLocation + Eye, Reference + Eye, TraceChannel, Params);
}

bool UAI_QueryTest_LineOfSight::IsItemValid_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	if (!bRequireLoS)
	{
		return true;
	}
	return HasLineOfSight(Context, ItemLocation);
}

float UAI_QueryTest_LineOfSight::ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	return HasLineOfSight(Context, ItemLocation) ? 1.f : 0.f;
}

//~ LosToTarget ---------------------------------------------------------------------------------

bool UAI_QueryTest_LosToTarget::IsItemValid_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	if (!bRequireLoS)
	{
		return true;
	}
	const AActor* Querier = Context.Querier.Get();
	const UWorld* World = Querier ? Querier->GetWorld() : nullptr;
	if (!World)
	{
		return true;
	}
	const FVector Eye(0.f, 0.f, EyeHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AI_QueryLosTarget), false);
	if (Querier)
	{
		Params.AddIgnoredActor(Querier);
	}
	return !World->LineTraceTestByChannel(ItemLocation + Eye, Context.TargetLocation + Eye, TraceChannel, Params);
}

float UAI_QueryTest_LosToTarget::ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const
{
	return IsItemValid_Implementation(Context, ItemLocation, ItemActor) ? 1.f : 0.f;
}

//~ Density -------------------------------------------------------------------------------------

float UAI_QueryTest_Density::ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	AActor* Querier = Context.Querier.Get();
	UWorld* World = Querier ? Querier->GetWorld() : nullptr;
	if (!World)
	{
		return 0.f;
	}

	// Friendly filtering needs the querier to implement the team seam (BlueprintNativeEvent: use Execute_).
	const bool bUseTeam = bFriendlyOnly && Querier && Querier->GetClass()->ImplementsInterface(USeam_TeamAffinity::StaticClass());

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(OverlapChannel);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AI_QueryDensity), false);
	Params.AddIgnoredActor(Querier);

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, ItemLocation, FQuat::Identity, ObjParams,
		FCollisionShape::MakeSphere(FMath::Max(1.f, CountRadius)), Params);

	int32 Count = 0;
	for (const FOverlapResult& Result : Overlaps)
	{
		AActor* Other = Result.GetActor();
		if (!Other || Other == Querier)
		{
			continue;
		}
		if (bUseTeam)
		{
			// Only count friendlies (AreFriendly is BlueprintNativeEvent; dispatch via Execute_).
			if (!ISeam_TeamAffinity::Execute_AreFriendly(Querier, Querier, Other))
			{
				continue;
			}
		}
		++Count;
	}

	// Raw density score; the subsystem normalizes across candidates (and bInvert flips "spread out").
	return static_cast<float>(Count);
}

//~ CoverScore ----------------------------------------------------------------------------------

float UAI_QueryTest_CoverScore::ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* /*ItemActor*/) const
{
	UDP_ServiceLocatorSubsystem* Locator = ResolveLocator(Context);
	if (!Locator)
	{
		return 0.f; // no service locator → no cover info, neutral contribution
	}

	UObject* Provider = Locator->ResolveService(AINativeTags::Service_AI_Cover);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(USeam_CoverProvider::StaticClass()))
	{
		return 0.f; // no cover provider registered → neutral
	}

	const FVector Threat = bThreatIsTarget ? Context.TargetLocation : Context.QuerierLocation;
	if (ISeam_CoverProvider* Cover = Cast<ISeam_CoverProvider>(Provider))
	{
		return FMath::Max(0.f, Cover->ScoreCoverAt(ItemLocation, Threat));
	}
	return 0.f;
}
