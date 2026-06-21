// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Cover/AI_CoverSubsystem.h"
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
	/** Defensive fallback if the settings CDO is somehow null (never in a running game). */
	constexpr float GCoverProtectionWeight_Fallback = 0.6f;
}

void UAI_CoverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSelfAsService();
}

void UAI_CoverSubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (Locator->ResolveService(AINativeTags::Service_AI_Cover) == this)
		{
			Locator->UnregisterService(AINativeTags::Service_AI_Cover);
		}
	}
	Points.Reset();
	Super::Deinitialize();
}

UDP_ServiceLocatorSubsystem* UAI_CoverSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

void UAI_CoverSubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		// WeakObserved: the locator must not keep this world-lifetime subsystem alive across travel.
		Locator->RegisterService(AINativeTags::Service_AI_Cover, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ Indexing ------------------------------------------------------------------------------------

void UAI_CoverSubsystem::RebuildCoverIndex() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TMap<FGuid, TWeakObjectPtr<AAI_CoverPoint>>& Index = const_cast<UAI_CoverSubsystem*>(this)->Points;
	Index.Reset();
	for (TActorIterator<AAI_CoverPoint> It(World); It; ++It)
	{
		AAI_CoverPoint* Point = *It;
		if (Point && Point->GetCoverId().IsValid())
		{
			Index.Add(Point->GetCoverId().Value, Point);
		}
	}
}

AAI_CoverPoint* UAI_CoverSubsystem::FindCoverPointById(const FSeam_EntityId& CoverId) const
{
	if (!CoverId.IsValid())
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<AAI_CoverPoint>* Found = Points.Find(CoverId.Value))
	{
		if (AAI_CoverPoint* Live = Found->Get())
		{
			return Live;
		}
	}
	RebuildCoverIndex();
	if (const TWeakObjectPtr<AAI_CoverPoint>* Found = Points.Find(CoverId.Value))
	{
		return Found->Get();
	}
	return nullptr;
}

int32 UAI_CoverSubsystem::GetCoverPointCount() const
{
	RebuildCoverIndex();
	return Points.Num();
}

//~ Scoring -------------------------------------------------------------------------------------

float UAI_CoverSubsystem::ScorePoint(const AAI_CoverPoint& Point, const FVector& Origin, const FVector& ThreatLocation) const
{
	const FVector PointLoc = Point.GetActorLocation();

	// Threat direction at the cover: from the cover toward the threat.
	const FVector ThreatDir = (ThreatLocation - PointLoc).GetSafeNormal();

	// Protection quality: 1 if the point shields the threat direction, else a small floor so it can still
	// be considered when nothing better exists.
	const float Protection = Point.ProtectsAgainst(ThreatDir, ProtectionMinDot) ? 1.f : 0.15f;

	// Distance quality: closer to the seeker is better, normalized by a soft scale.
	const float Dist = FVector::Dist(PointLoc, Origin);
	const float DistQuality = 1.f / (1.f + Dist / 1000.f);

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const float ProtW = FMath::Clamp(Settings ? Settings->CoverProtectionWeight : GCoverProtectionWeight_Fallback, 0.f, 1.f);

	return ProtW * Protection + (1.f - ProtW) * DistQuality;
}

float UAI_CoverSubsystem::ScoreCoverAt(const FVector& Location, const FVector& ThreatLocation) const
{
	// Score the BEST nearby authored cover point covering Location; 0 if no point is near Location.
	RebuildCoverIndex();
	float Best = 0.f;
	for (const TPair<FGuid, TWeakObjectPtr<AAI_CoverPoint>>& Pair : Points)
	{
		const AAI_CoverPoint* Point = Pair.Value.Get();
		if (!Point)
		{
			continue;
		}
		// Only points effectively AT this location (within a small bind radius) describe Location's cover.
		if (FVector::DistSquared(Point->GetActorLocation(), Location) <= FMath::Square(200.f))
		{
			Best = FMath::Max(Best, ScorePoint(*Point, Location, ThreatLocation));
		}
	}
	return Best;
}

//~ Find best -----------------------------------------------------------------------------------

AAI_CoverPoint* UAI_CoverSubsystem::FindBestCoverPoint(const FVector& Origin, const FVector& ThreatLocation, float Radius) const
{
	RebuildCoverIndex();
	AAI_CoverPoint* BestPoint = nullptr;
	float BestScore = 0.f;
	const float RadiusSq = (Radius > 0.f) ? FMath::Square(Radius) : TNumericLimits<float>::Max();

	for (const TPair<FGuid, TWeakObjectPtr<AAI_CoverPoint>>& Pair : Points)
	{
		AAI_CoverPoint* Point = Pair.Value.Get();
		if (!Point || Point->IsClaimed())
		{
			continue; // only unclaimed cover is selectable
		}
		if (FVector::DistSquared(Point->GetActorLocation(), Origin) > RadiusSq)
		{
			continue;
		}
		const float Score = ScorePoint(*Point, Origin, ThreatLocation);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestPoint = Point;
		}
	}
	return BestPoint;
}

bool UAI_CoverSubsystem::FindBestCover(const FVector& Origin, const FVector& ThreatLocation, float Radius,
	FTransform& OutCoverTransform, FSeam_EntityId& OutCoverId) const
{
	if (const AAI_CoverPoint* Point = FindBestCoverPoint(Origin, ThreatLocation, Radius))
	{
		OutCoverTransform = Point->GetStandTransform();
		OutCoverId = Point->GetCoverId();
		return true;
	}
	return false;
}

//~ Spawning ------------------------------------------------------------------------------------

AAI_CoverPoint* UAI_CoverSubsystem::SpawnCoverPoint(const FTransform& Where, FGameplayTag CoverTypeTag, const TArray<FVector>& ProtectedDirections)
{
	if (!HasWorldAuthority())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient; // runtime coordination state, not a level actor

	AAI_CoverPoint* Point = World->SpawnActor<AAI_CoverPoint>(AAI_CoverPoint::StaticClass(), Where, SpawnParams);
	if (!Point)
	{
		UE_LOG(LogDP, Error, TEXT("AI CoverSubsystem: failed to spawn cover point."));
		return nullptr;
	}

	Point->CoverTypeTag = CoverTypeTag;
	Point->ProtectedDirections = ProtectedDirections;
	Point->EnsureCoverId();
	Points.Add(Point->GetCoverId().Value, Point);
	return Point;
}

//~ Debug ---------------------------------------------------------------------------------------

FString UAI_CoverSubsystem::GetDPDebugString_Implementation() const
{
	RebuildCoverIndex();
	int32 Claimed = 0;
	for (const TPair<FGuid, TWeakObjectPtr<AAI_CoverPoint>>& Pair : Points)
	{
		if (const AAI_CoverPoint* Point = Pair.Value.Get())
		{
			Claimed += Point->IsClaimed() ? 1 : 0;
		}
	}
	return FString::Printf(TEXT("AI Cover: %d points (%d claimed)"), Points.Num(), Claimed);
}
