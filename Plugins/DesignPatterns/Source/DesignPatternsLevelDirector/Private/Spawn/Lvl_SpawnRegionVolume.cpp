// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/Lvl_SpawnRegionVolume.h"
#include "Spawn/Lvl_SpawnPointComponent.h"

#include "DesignPatternsLevelDirectorModule.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "Components/BrushComponent.h"
#include "Math/RandomStream.h"
#include "Engine/World.h"

ALvl_SpawnRegionVolume::ALvl_SpawnRegionVolume()
{
	// Spawn regions are pure authoring volumes: no tick, no replication, no save participation.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	if (UBrushComponent* Brush = GetBrushComponent())
	{
		// Editor-only query volume: no collision response so it never blocks gameplay.
		Brush->SetCollisionProfileName(TEXT("NoCollision"));
		Brush->SetGenerateOverlapEvents(false);
	}
}

//~ AActor lifecycle -------------------------------------------------------------------------------

void ALvl_SpawnRegionVolume::BeginPlay()
{
	Super::BeginPlay();
	RegisterAsProvider();
}

void ALvl_SpawnRegionVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterAsProvider();
	Super::EndPlay(EndPlayReason);
}

void ALvl_SpawnRegionVolume::RegisterAsProvider()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// The locator is single-slot per key; a level may contain several regions. We register as
		// WeakObserved with override so the MOST RECENTLY begun region is resolvable by tag for the
		// simple single-provider case. For multi-region aggregation, the spawn director iterates
		// ALvl_SpawnRegionVolume actors (or a project-supplied aggregator) rather than the locator.
		// Either way this registration never keeps a dead world's actor alive (rule 3).
		Locator->RegisterService(LvlTags::Service_SpawnRegionProvider, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void ALvl_SpawnRegionVolume::UnregisterAsProvider()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only clear the slot if it still points at us (a later region may have overridden it).
		if (Locator->ResolveService(LvlTags::Service_SpawnRegionProvider) == this)
		{
			Locator->UnregisterService(LvlTags::Service_SpawnRegionProvider);
		}
	}
}

//~ Filtering / transform helpers ------------------------------------------------------------------

bool ALvl_SpawnRegionVolume::RegionMatchesFilter(FGameplayTag Filter) const
{
	if (!Filter.IsValid())
	{
		return true; // match-all
	}
	return RegionFilterTags.HasTag(Filter);
}

FTransform ALvl_SpawnRegionVolume::MakeSpawnTransform(const FVector& Location) const
{
	const FRotator Rot(0.f, FacingYaw, 0.f);
	return FTransform(Rot, Location, FVector::OneVector);
}

bool ALvl_SpawnRegionVolume::IsLocationInside(const FVector& WorldLocation) const
{
	// AVolume::EncompassesPoint wraps the brush geometry test (rule 5: wrap engine, don't reinvent).
	return EncompassesPoint(WorldLocation);
}

//~ ILvl_SpawnRegionProvider -----------------------------------------------------------------------

void ALvl_SpawnRegionVolume::GetSpawnPoints_Implementation(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const
{
	// Region-level filter gate first: an unrelated filter skips the whole region cheaply.
	if (!RegionMatchesFilter(Filter))
	{
		// Explicit points may still individually match a finer filter even if the region's coarse
		// tags don't, so we only early-out when there are no explicit points to consult.
		if (RegionMode == ELvl_SpawnRegionMode::SampledOnly)
		{
			return;
		}
	}

	if (RegionMode == ELvl_SpawnRegionMode::ExplicitPointsOnly || RegionMode == ELvl_SpawnRegionMode::Both)
	{
		GetExplicitPoints(Filter, OutTransforms);
	}

	if (RegionMode == ELvl_SpawnRegionMode::SampledOnly || RegionMode == ELvl_SpawnRegionMode::Both)
	{
		// Sampled points carry only the region's tags, so they require the region to match.
		if (RegionMatchesFilter(Filter))
		{
			GetSampledPoints(Filter, OutTransforms);
		}
	}
}

void ALvl_SpawnRegionVolume::GetExplicitPoints(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const
{
	TArray<ULvl_SpawnPointComponent*> Points;
	GetComponents<ULvl_SpawnPointComponent>(Points);

	for (const ULvl_SpawnPointComponent* Point : Points)
	{
		if (!Point)
		{
			continue;
		}

		// A point matches if its own tags match the filter, OR (when the point has no own filter tags)
		// it inherits the region's match decision. This lets a designer place generic points that take
		// the region's team while still allowing per-point overrides.
		const bool bPointMatches = Point->MatchesFilter(Filter) ||
			(Point->FilterTags.IsEmpty() && Point->bEnabled && RegionMatchesFilter(Filter));

		if (bPointMatches)
		{
			// Preserve the point's authored rotation but honour the region facing when the point has
			// the default (identity) rotation, so generic points still face consistently.
			FTransform T = Point->GetSpawnTransform();
			if (T.GetRotation().IsIdentity(KINDA_SMALL_NUMBER) && !FMath::IsNearlyZero(FacingYaw))
			{
				T.SetRotation(FQuat(FRotator(0.f, FacingYaw, 0.f)));
			}
			OutTransforms.Add(T);
		}
	}
}

void ALvl_SpawnRegionVolume::GetSampledPoints(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const
{
	if (SampleCount <= 0)
	{
		return;
	}

	const UBrushComponent* Brush = GetBrushComponent();
	if (!Brush)
	{
		return;
	}

	// World-space bounds of the brush to sample within; EncompassesPoint rejects outside-the-brush hits.
	const FBox Bounds = Brush->Bounds.GetBox();
	const FVector Extent = Bounds.GetExtent();
	if (Extent.IsNearlyZero())
	{
		return;
	}

	// Deterministic RNG seeded from data (SamplingSeed) so results are reproducible (rule 7 spirit:
	// deterministic placement via a seeded RNG). Mix in the actor's stable name hash so two regions
	// sharing a seed still differ.
	const int32 NameHash = GetFName().IsNone() ? 0 : GetTypeHash(GetFName());
	FRandomStream Rng(SamplingSeed ^ NameHash);

	const FVector Center = Bounds.GetCenter();
	const float MinSpacingSq = FMath::Square(FMath::Max(0.f, MinSampleSpacing));
	const int32 MaxAttempts = FMath::Max(1, MaxSampleAttemptsPerPoint);

	TArray<FVector> Accepted;
	Accepted.Reserve(SampleCount);

	for (int32 i = 0; i < SampleCount; ++i)
	{
		bool bPlaced = false;
		for (int32 Attempt = 0; Attempt < MaxAttempts && !bPlaced; ++Attempt)
		{
			const FVector Candidate(
				Center.X + Rng.FRandRange(-Extent.X, Extent.X),
				Center.Y + Rng.FRandRange(-Extent.Y, Extent.Y),
				Center.Z + Rng.FRandRange(-Extent.Z, Extent.Z));

			if (!IsLocationInside(Candidate))
			{
				continue;
			}

			bool bTooClose = false;
			if (MinSpacingSq > 0.f)
			{
				for (const FVector& Existing : Accepted)
				{
					if (FVector::DistSquared(Existing, Candidate) < MinSpacingSq)
					{
						bTooClose = true;
						break;
					}
				}
			}

			if (!bTooClose)
			{
				Accepted.Add(Candidate);
				OutTransforms.Add(MakeSpawnTransform(Candidate));
				bPlaced = true;
			}
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[LevelDirector] Region '%s' sampled %d/%d points (filter=%s)."),
		*GetName(), Accepted.Num(), SampleCount, *Filter.ToString());
}
