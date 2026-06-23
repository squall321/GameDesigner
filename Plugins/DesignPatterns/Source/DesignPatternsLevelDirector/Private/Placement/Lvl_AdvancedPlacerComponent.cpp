// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/Lvl_AdvancedPlacerComponent.h"
#include "Placement/Lvl_ScatterModifierDataAsset.h"
#include "Placement/Lvl_BiomeMaskComponent.h"
#include "Placement/Lvl_ProceduralPlacerComponent.h"

#include "Core/DPLog.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

namespace
{
	/** Defensive hard ceiling so a misconfigured modifier can never run an unbounded sampling loop. */
	constexpr int32 GAbsoluteScatterCeiling = 100000;
}

ULvl_AdvancedPlacerComponent::ULvl_AdvancedPlacerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void ULvl_AdvancedPlacerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!TargetPlacer)
	{
		TargetPlacer = ResolveTargetPlacer();
	}
	if (!BiomeMaskSource)
	{
		if (const AActor* Owner = GetOwner())
		{
			BiomeMaskSource = Owner->FindComponentByClass<ULvl_BiomeMaskComponent>();
		}
	}

	if (bGenerateOnBeginPlay && HasWorldAuthority())
	{
		GenerateScatter();
	}
}

bool ULvl_AdvancedPlacerComponent::HasWorldAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

ULvl_ProceduralPlacerComponent* ULvl_AdvancedPlacerComponent::ResolveTargetPlacer() const
{
	if (TargetPlacer)
	{
		return TargetPlacer;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<ULvl_ProceduralPlacerComponent>();
	}
	return nullptr;
}

int32 ULvl_AdvancedPlacerComponent::GetEffectiveSeed() const
{
	if (SeedOverride >= 0)
	{
		return SeedOverride;
	}
	// Derive a stable seed from the owner name (matches the base placer's seed policy).
	const AActor* Owner = GetOwner();
	const FString Key = Owner ? Owner->GetName() : GetName();
	return static_cast<int32>(GetTypeHash(Key) & 0x7fffffff);
}

bool ULvl_AdvancedPlacerComponent::GenerateScatter()
{
	// AUTHORITY GUARD AT THE TOP — clients never place.
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!ScatterModifier)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl AdvancedPlacer (%s): no ScatterModifier."), *GetNameSafe(GetOwner()));
		return false;
	}
	if (!ScatterModifier->HasUsableClass())
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl AdvancedPlacer (%s): modifier %s has no usable class tag."),
			*GetNameSafe(GetOwner()), *ScatterModifier->DataTag.ToString());
		return false;
	}

	ULvl_ProceduralPlacerComponent* Placer = ResolveTargetPlacer();
	if (!Placer)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl AdvancedPlacer (%s): no target ULvl_ProceduralPlacerComponent."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	const int32 Seed = GetEffectiveSeed();
	FRandomStream Stream(Seed);

	// 1) Bridson Poisson-disk candidates over the modifier's XY area.
	TArray<FVector> Candidates;
	GeneratePoissonCandidates(Stream, Candidates);

	// 2) Build the manifest from accepted, gated, projected candidates.
	FLvl_PlacementManifest Manifest;
	Manifest.RuleSetTag = ScatterModifier->DataTag;
	Manifest.RegionTag = RegionTag;
	Manifest.RandomSeed = Seed;

	const TArray<FGameplayTag>& ClassTags = ScatterModifier->ScatterClassTags;
	int32 PlacedCount = 0;

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FVector& Candidate = Candidates[Index];

		// Distance-field + biome gating (deterministic via the shared stream).
		if (!PassesBiomeAndDistanceField(Stream, Candidate))
		{
			continue;
		}

		// Surface projection.
		FVector FinalLoc = Candidate;
		if (!ProjectToSurface(Candidate, FinalLoc))
		{
			continue;
		}

		// Pick a class tag deterministically.
		FGameplayTag ChosenTag;
		{
			// Build the usable subset once per candidate is cheap relative to the trace above.
			TArray<FGameplayTag, TInlineAllocator<16>> Usable;
			for (const FGameplayTag& Tag : ClassTags)
			{
				if (Tag.IsValid())
				{
					Usable.Add(Tag);
				}
			}
			if (Usable.Num() == 0)
			{
				continue;
			}
			ChosenTag = Usable[Stream.RandRange(0, Usable.Num() - 1)];
		}

		// Deterministic yaw for variation.
		const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(Stream.FRandRange(0.f, 360.f)));
		const FTransform Xform(Yaw, FinalLoc, FVector::OneVector);

		Manifest.Entries.Emplace(ChosenTag, Xform, MakeDeterministicScatterId(Seed, Index));
		++PlacedCount;
	}

	if (PlacedCount == 0)
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl AdvancedPlacer (%s): no candidates survived gating."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// 3) Hand the manifest to the base placer (spawn + pool + save + bus all flow through it).
	const int32 Spawned = Placer->RestoreFromManifest(Manifest);
	UE_LOG(LogDP, Log, TEXT("Lvl AdvancedPlacer (%s): scattered %d, base placer spawned %d (seed %d)."),
		*GetNameSafe(GetOwner()), PlacedCount, Spawned, Seed);
	return Spawned > 0;
}

void ULvl_AdvancedPlacerComponent::GeneratePoissonCandidates(FRandomStream& Stream, TArray<FVector>& OutCandidates) const
{
	if (!ScatterModifier)
	{
		return;
	}

	const float Radius = FMath::Max(1.f, ScatterModifier->PoissonMinRadius);
	const float ExtentX = FMath::Max(1.f, static_cast<float>(ScatterModifier->SampleAreaExtent.X));
	const float ExtentY = FMath::Max(1.f, static_cast<float>(ScatterModifier->SampleAreaExtent.Y));
	const int32 MaxPoints = FMath::Clamp(ScatterModifier->MaxScatterPoints, 1, GAbsoluteScatterCeiling);
	const int32 K = FMath::Max(1, ScatterModifier->PoissonMaxAttempts);

	// Bridson over a [-Extent, +Extent] XY area, using a background grid of cell size r/sqrt(2).
	const float CellSize = Radius / UE_SQRT_2;
	const float Width = 2.f * ExtentX;
	const float Height = 2.f * ExtentY;
	const int32 GridW = FMath::Max(1, FMath::CeilToInt(Width / CellSize));
	const int32 GridH = FMath::Max(1, FMath::CeilToInt(Height / CellSize));

	// -1 = empty; otherwise an index into Points (the sample owning that grid cell).
	TArray<int32> Grid;
	Grid.Init(INDEX_NONE, GridW * GridH);

	TArray<FVector2D> Points;      // local-space (centred on owner) accepted points
	TArray<int32> ActiveList;      // indices into Points still spawning candidates

	auto GridIndexOf = [&](const FVector2D& P) -> int32
	{
		const int32 GX = FMath::Clamp(FMath::FloorToInt((P.X + ExtentX) / CellSize), 0, GridW - 1);
		const int32 GY = FMath::Clamp(FMath::FloorToInt((P.Y + ExtentY) / CellSize), 0, GridH - 1);
		return GY * GridW + GX;
	};

	auto FitsRadius = [&](const FVector2D& P) -> bool
	{
		const int32 GX = FMath::Clamp(FMath::FloorToInt((P.X + ExtentX) / CellSize), 0, GridW - 1);
		const int32 GY = FMath::Clamp(FMath::FloorToInt((P.Y + ExtentY) / CellSize), 0, GridH - 1);
		const float R2 = Radius * Radius;
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			for (int32 DX = -2; DX <= 2; ++DX)
			{
				const int32 NX = GX + DX;
				const int32 NY = GY + DY;
				if (NX < 0 || NX >= GridW || NY < 0 || NY >= GridH)
				{
					continue;
				}
				const int32 Existing = Grid[NY * GridW + NX];
				if (Existing != INDEX_NONE && FVector2D::DistSquared(Points[Existing], P) < R2)
				{
					return false;
				}
			}
		}
		return true;
	};

	// Seed point at a deterministic location near the centre.
	FVector2D Seed0(Stream.FRandRange(-ExtentX, ExtentX), Stream.FRandRange(-ExtentY, ExtentY));
	Points.Add(Seed0);
	ActiveList.Add(0);
	Grid[GridIndexOf(Seed0)] = 0;

	const FTransform OwnerXform = GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity;

	while (ActiveList.Num() > 0 && Points.Num() < MaxPoints)
	{
		const int32 ActiveIdx = Stream.RandRange(0, ActiveList.Num() - 1);
		const int32 PointIdx = ActiveList[ActiveIdx];
		const FVector2D Origin = Points[PointIdx];

		bool bFound = false;
		for (int32 Attempt = 0; Attempt < K; ++Attempt)
		{
			const float Angle = Stream.FRandRange(0.f, 2.f * PI);
			const float Dist = Radius * (1.f + Stream.FRand()); // annulus [r, 2r]
			const FVector2D Cand(Origin.X + FMath::Cos(Angle) * Dist, Origin.Y + FMath::Sin(Angle) * Dist);

			if (Cand.X < -ExtentX || Cand.X > ExtentX || Cand.Y < -ExtentY || Cand.Y > ExtentY)
			{
				continue;
			}
			if (!FitsRadius(Cand))
			{
				continue;
			}

			const int32 NewIdx = Points.Add(Cand);
			Grid[GridIndexOf(Cand)] = NewIdx;
			ActiveList.Add(NewIdx);
			bFound = true;
			if (Points.Num() >= MaxPoints)
			{
				break;
			}
		}

		if (!bFound)
		{
			ActiveList.RemoveAtSwap(ActiveIdx);
		}
	}

	// Transform accepted local points to world.
	OutCandidates.Reserve(Points.Num());
	for (const FVector2D& P : Points)
	{
		OutCandidates.Add(OwnerXform.TransformPosition(FVector(P.X, P.Y, 0.0)));
	}
}

bool ULvl_AdvancedPlacerComponent::PassesBiomeAndDistanceField(FRandomStream& Stream, const FVector& WorldLoc) const
{
	if (!ScatterModifier)
	{
		return false;
	}

	// Distance-field gate: normalized distance from the owner-area centre.
	const FVector Centre = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const float ExtentX = FMath::Max(1.f, static_cast<float>(ScatterModifier->SampleAreaExtent.X));
	const float ExtentY = FMath::Max(1.f, static_cast<float>(ScatterModifier->SampleAreaExtent.Y));
	const float MaxExtent = FMath::Max(ExtentX, ExtentY);
	const float NormDist = FMath::Clamp(static_cast<float>(FVector::Dist2D(WorldLoc, Centre)) / MaxExtent, 0.f, 1.f);
	const float Accept = ScatterModifier->SampleDistanceFieldWeight(NormDist);
	if (Stream.FRand() > Accept)
	{
		return false;
	}

	// Biome gate (only when a mask source and an allow-list / min-weight are configured).
	if (BiomeMaskSource &&
		(!ScatterModifier->AllowedBiomeTags.IsEmpty() || ScatterModifier->MinBiomeWeight > 0.f))
	{
		float Weight = 0.f;
		const FGameplayTag Biome = BiomeMaskSource->GetBiomeAt(WorldLoc, Weight);
		if (Weight < ScatterModifier->MinBiomeWeight)
		{
			return false;
		}
		if (!ScatterModifier->AllowedBiomeTags.IsEmpty())
		{
			if (!Biome.IsValid() || !ScatterModifier->AllowedBiomeTags.HasTag(Biome))
			{
				return false;
			}
		}
	}

	return true;
}

bool ULvl_AdvancedPlacerComponent::ProjectToSurface(const FVector& Candidate, FVector& OutLocation) const
{
	OutLocation = Candidate;
	if (!ScatterModifier || !ScatterModifier->bProjectToSurface)
	{
		return true; // no projection requested -> accept at the candidate height
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = Candidate + FVector(0.0, 0.0, ScatterModifier->TraceStartHeight);
	const FVector End = Start - FVector(0.0, 0.0, ScatterModifier->TraceDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LvlAdvancedPlacerSurfaceTrace), /*bTraceComplex=*/false);
	if (const AActor* Owner = GetOwner())
	{
		Params.AddIgnoredActor(Owner);
	}

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ScatterModifier->SurfaceTraceChannel.GetValue(), Params))
	{
		OutLocation = Hit.ImpactPoint;
		return true;
	}
	// Missed: reject (advanced scatter is surface content by default).
	return false;
}

FGuid ULvl_AdvancedPlacerComponent::MakeDeterministicScatterId(int32 Seed, int32 CandidateIndex)
{
	// Mirrors the base placer's deterministic-id mixing so save/restore reconciles entries identically.
	const uint32 A = static_cast<uint32>(Seed);
	const uint32 B = static_cast<uint32>(CandidateIndex);
	return FGuid(
		HashCombine(A, 0x9E3779B9u),
		HashCombine(A ^ 0x85EBCA6Bu, B),
		HashCombine(B, 0xC2B2AE35u),
		HashCombine(A * 2654435761u, B * 40503u));
}
