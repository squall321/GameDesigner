// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/Lvl_ProceduralPlacerComponent.h"
#include "Placement/Lvl_PlacementRuleSet.h"
#include "DesignPatternsLevelDirectorNativeTags.h"
#include "Lvl_BusPayloads.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Factory/DPSpawnFactorySubsystem.h"
#include "Factory/DPSpawnRecipe.h"

// Seams are resolved in the .cpp only (no hard module coupling).
#include "Activation/Seam_ActivationGate.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Grid/Seam_GridCoord.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Square centimetres per square metre, for converting density-per-m^2 to a candidate count. */
	constexpr double GSquareCmPerSquareMetre = 10000.0;

	/** A defensive hard ceiling so a misconfigured rule set can never request an unbounded candidate loop. */
	constexpr int32 GAbsoluteCandidateCeiling = 100000;
}

ULvl_ProceduralPlacerComponent::ULvl_ProceduralPlacerComponent()
{
	// The placer does no per-frame work: a pass is an explicit one-shot. Disable ticking entirely.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// The component itself is not replicated; placement is authority-only and the spawned actors
	// replicate themselves. Make that explicit so it is not accidentally flagged for replication.
	SetIsReplicatedByDefault(false);
}

void ULvl_ProceduralPlacerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bGenerateOnBeginPlay && HasWorldAuthority())
	{
		GeneratePlacement();
	}
}

void ULvl_ProceduralPlacerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bClearOnEndPlay && HasWorldAuthority())
	{
		DestroySpawnedActors();
		Manifest.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

bool ULvl_ProceduralPlacerComponent::HasWorldAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

UDP_ServiceLocatorSubsystem* ULvl_ProceduralPlacerComponent::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

UDP_SpawnFactorySubsystem* ULvl_ProceduralPlacerComponent::GetFactory() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UDP_SpawnFactorySubsystem>(this);
}

FGameplayTag ULvl_ProceduralPlacerComponent::GetEffectiveRegionTag() const
{
	if (RegionTagOverride.IsValid())
	{
		return RegionTagOverride;
	}
	return RuleSet ? RuleSet->DefaultRegionTag : FGameplayTag();
}

int32 ULvl_ProceduralPlacerComponent::GetEffectiveSeed() const
{
	if (SeedOverride >= 0)
	{
		return SeedOverride;
	}
	if (RuleSet && RuleSet->RandomSeed != 0)
	{
		return RuleSet->RandomSeed;
	}
	// Seed policy 0: derive a stable seed from the owner's name so the layout is reproducible but
	// unique per placer. GetTypeHash over the name is stable for a given actor label.
	const AActor* Owner = GetOwner();
	const FString Key = Owner ? Owner->GetName() : GetName();
	return static_cast<int32>(GetTypeHash(Key) & 0x7fffffff);
}

//~ Gate / tile-provider resolution -----------------------------------------------------------------

bool ULvl_ProceduralPlacerComponent::IsGateOpen(const FGameplayTag& GateKey) const
{
	// An invalid key means "ungated": always open.
	if (!GateKey.IsValid())
	{
		return true;
	}

	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return true; // documented inert default: no locator -> gate open
	}

	UObject* GateObj = Locator->ResolveService(LvlNativeTags::Service_Lvl_ActivationGate);
	if (!GateObj || !GateObj->GetClass()->ImplementsInterface(USeam_ActivationGate::StaticClass()))
	{
		return true; // gate seam unresolved -> default open (content active)
	}
	return ISeam_ActivationGate::Execute_IsGateOpen(GateObj, GateKey);
}

UObject* ULvl_ProceduralPlacerComponent::ResolveTileProvider() const
{
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return nullptr;
	}
	UObject* ProviderObj = Locator->ResolveService(LvlNativeTags::Service_Lvl_TileProvider);
	if (ProviderObj && ProviderObj->GetClass()->ImplementsInterface(USeam_TileProviderRead::StaticClass()))
	{
		return ProviderObj;
	}
	return nullptr; // unresolved -> caller skips tile-mask validation (candidates pass the mask)
}

//~ Pass control ------------------------------------------------------------------------------------

bool ULvl_ProceduralPlacerComponent::GeneratePlacement()
{
	// AUTHORITY GUARD AT THE TOP — clients never place.
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!RuleSet)
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl Placer (%s): GeneratePlacement with no RuleSet."), *GetNameSafe(GetOwner()));
		return false;
	}
	if (!RuleSet->HasUsableClass())
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl Placer (%s): RuleSet %s has no usable class choice."),
			*GetNameSafe(GetOwner()), *RuleSet->DataTag.ToString());
		return false;
	}

	// Whole-pass activation gate (default open when unresolved).
	if (!IsGateOpen(RuleSet->GateKey))
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl Placer (%s): gate %s closed; placement suppressed."),
			*GetNameSafe(GetOwner()), *RuleSet->GateKey.ToString());
		return false;
	}

	// Re-running replaces the previous pass entirely.
	DestroySpawnedActors();
	Manifest.Reset();

	const int32 Seed = GetEffectiveSeed();
	Manifest.RuleSetTag = RuleSet->DataTag;
	Manifest.RegionTag = GetEffectiveRegionTag();
	Manifest.RandomSeed = Seed;

	// One stream drives the WHOLE pass so the layout is fully reproducible from (rule set, seed).
	FRandomStream Stream(Seed);

	TArray<FVector> Candidates;
	GenerateCandidates(*RuleSet, Stream, Candidates);

	UObject* TileProvider = ResolveTileProvider();

	// Accepted placements (for spacing tests).
	TArray<FVector> AcceptedLocations;
	AcceptedLocations.Reserve(FMath::Min(Candidates.Num(), RuleSet->MaxPlacements));

	const float MinSpacingSq = RuleSet->MinSpacing * RuleSet->MinSpacing;
	int32 Placed = 0;

	for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
	{
		if (Placed >= RuleSet->MaxPlacements)
		{
			break;
		}

		// Pick a weighted class choice (deterministic via the shared stream).
		const FLvl_PlacementClassChoice* Choice = RuleSet->PickClassChoice(Stream);
		if (!Choice || !Choice->IsUsable())
		{
			continue;
		}

		const FVector& Candidate = Candidates[CandidateIndex];

		// Spacing test against already-accepted placements.
		if (RuleSet->MinSpacing > 0.f)
		{
			bool bTooClose = false;
			for (const FVector& Accepted : AcceptedLocations)
			{
				if (FVector::DistSquared2D(Accepted, Candidate) < MinSpacingSq)
				{
					bTooClose = true;
					break;
				}
			}
			if (bTooClose)
			{
				continue;
			}
		}

		FTransform FinalTransform;
		if (!ValidateAndProject(*RuleSet, *Choice, Stream, TileProvider, Candidate, FinalTransform))
		{
			continue;
		}

		AActor* Spawned = SpawnEntry(*RuleSet, *Choice, FinalTransform);
		if (!Spawned)
		{
			continue;
		}

		// Record the result with a deterministic id (so save/restore reconciles entries).
		const FGuid PlacementId = MakeDeterministicPlacementId(Seed, CandidateIndex);
		Manifest.Entries.Emplace(Choice->ActorClassTag, FinalTransform, PlacementId);
		SpawnedActors.Add(Spawned);
		AcceptedLocations.Add(FinalTransform.GetLocation());
		++Placed;
	}

	BroadcastPlacementEvent(LvlNativeTags::Bus_Lvl_Placement_Generated, Placed);
	UE_LOG(LogDP, Log, TEXT("Lvl Placer (%s): placed %d/%d candidates (rule set %s, seed %d)."),
		*GetNameSafe(GetOwner()), Placed, Candidates.Num(), *RuleSet->DataTag.ToString(), Seed);

	return Placed > 0;
}

void ULvl_ProceduralPlacerComponent::ClearPlacement()
{
	if (!HasWorldAuthority())
	{
		return;
	}
	const int32 Had = Manifest.Num();
	DestroySpawnedActors();
	Manifest.Reset();
	if (Had > 0)
	{
		BroadcastPlacementEvent(LvlNativeTags::Bus_Lvl_Placement_Cleared, 0);
	}
}

int32 ULvl_ProceduralPlacerComponent::RestoreFromManifest(const FLvl_PlacementManifest& InManifest)
{
	// AUTHORITY GUARD AT THE TOP — a client-side load is a no-op (clients receive the actors via
	// replication once the server respawns them).
	if (!HasWorldAuthority())
	{
		return 0;
	}

	// Replace whatever is currently placed.
	DestroySpawnedActors();
	Manifest = InManifest;

	int32 Respawned = 0;
	for (const FLvl_PlacedEntry& Entry : Manifest.Entries)
	{
		if (!Entry.IsValid())
		{
			continue;
		}
		if (AActor* Spawned = SpawnFromEntry(Entry))
		{
			SpawnedActors.Add(Spawned);
			++Respawned;
		}
	}

	BroadcastPlacementEvent(LvlNativeTags::Bus_Lvl_Placement_Generated, Respawned);
	UE_LOG(LogDP, Log, TEXT("Lvl Placer (%s): restored %d/%d manifest entries (rule set %s, seed %d)."),
		*GetNameSafe(GetOwner()), Respawned, Manifest.Num(), *Manifest.RuleSetTag.ToString(), Manifest.RandomSeed);

	return Respawned;
}

//~ Candidate generation ----------------------------------------------------------------------------

void ULvl_ProceduralPlacerComponent::GenerateCandidates(const ULvl_PlacementRuleSet& Rules,
	FRandomStream& Stream, TArray<FVector>& OutCandidates) const
{
	switch (Rules.Source)
	{
	case ELvl_PlacementSource::SplinePath:
		GenerateSplineCandidates(Rules, Stream, OutCandidates);
		// SplinePath falls back to area scatter if the owner has no usable spline.
		if (OutCandidates.Num() == 0)
		{
			GenerateAreaCandidates(Rules, Stream, OutCandidates);
		}
		break;

	case ELvl_PlacementSource::RadialArea:
	case ELvl_PlacementSource::BoxVolume:
	default:
		GenerateAreaCandidates(Rules, Stream, OutCandidates);
		break;
	}
}

void ULvl_ProceduralPlacerComponent::GenerateAreaCandidates(const ULvl_PlacementRuleSet& Rules,
	FRandomStream& Stream, TArray<FVector>& OutCandidates) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const FTransform OwnerXform = Owner->GetActorTransform();

	// Derive the candidate count from the density-per-m^2 over the source area.
	double AreaCm2 = 0.0;
	if (Rules.Source == ELvl_PlacementSource::RadialArea)
	{
		AreaCm2 = PI * static_cast<double>(Rules.LocalRadius) * static_cast<double>(Rules.LocalRadius);
	}
	else
	{
		// Box: full XY footprint (extent is a HALF-extent).
		AreaCm2 = (2.0 * Rules.BoxExtent.X) * (2.0 * Rules.BoxExtent.Y);
	}
	const double AreaM2 = AreaCm2 / GSquareCmPerSquareMetre;
	int32 Count = FMath::RoundToInt(AreaM2 * static_cast<double>(Rules.DensityPerSquareMetre));
	Count = FMath::Clamp(Count, 0, FMath::Min(Rules.MaxCandidates, GAbsoluteCandidateCeiling));

	OutCandidates.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector Local;
		if (Rules.Source == ELvl_PlacementSource::RadialArea)
		{
			// Uniform disc sample (sqrt for uniform area density).
			const float Angle = Stream.FRandRange(0.f, 2.f * PI);
			const float Dist = Rules.LocalRadius * FMath::Sqrt(Stream.FRand());
			Local = FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.0);
		}
		else
		{
			Local = FVector(
				Stream.FRandRange(-Rules.BoxExtent.X, Rules.BoxExtent.X),
				Stream.FRandRange(-Rules.BoxExtent.Y, Rules.BoxExtent.Y),
				Stream.FRandRange(-Rules.BoxExtent.Z, Rules.BoxExtent.Z));
		}
		OutCandidates.Add(OwnerXform.TransformPosition(Local));
	}
}

void ULvl_ProceduralPlacerComponent::GenerateSplineCandidates(const ULvl_PlacementRuleSet& Rules,
	FRandomStream& Stream, TArray<FVector>& OutCandidates) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const USplineComponent* Spline = Owner->FindComponentByClass<USplineComponent>();
	if (!Spline)
	{
		return; // caller falls back to area scatter
	}

	const float Length = Spline->GetSplineLength();
	const float Spacing = FMath::Max(1.f, Rules.SplineSpacing);
	int32 SampleCount = FMath::FloorToInt(Length / Spacing) + 1;
	SampleCount = FMath::Clamp(SampleCount, 0, FMath::Min(Rules.MaxCandidates, GAbsoluteCandidateCeiling));

	OutCandidates.Reserve(SampleCount);
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const float Distance = FMath::Min(Index * Spacing, Length);
		const FVector OnSpline = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		FVector Candidate = OnSpline;
		if (Rules.SplineLateralJitter > 0.f)
		{
			// Jitter laterally in the spline's local right direction at this distance.
			const FVector Right = Spline->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
			const float Lateral = Stream.FRandRange(-Rules.SplineLateralJitter, Rules.SplineLateralJitter);
			Candidate += Right * Lateral;
		}
		OutCandidates.Add(Candidate);
	}
}

//~ Validation / projection -------------------------------------------------------------------------

bool ULvl_ProceduralPlacerComponent::PassesTileMask(const ULvl_PlacementRuleSet& Rules,
	UObject* TileProvider, const FVector& WorldLocation) const
{
	// No provider: tile-mask validation is skipped (the seam is the only source of tile types, so
	// without it every candidate passes the mask; the surface trace still gates height).
	if (!TileProvider)
	{
		return true;
	}

	const FSeam_CellCoord Cell = ISeam_TileProviderRead::Execute_WorldToCell(TileProvider, WorldLocation);

	if (!ISeam_TileProviderRead::Execute_IsValidCell(TileProvider, Cell))
	{
		// Out-of-bounds candidate: rejected unless the rule set tolerates unknown cells.
		return !Rules.bRejectUnknownCells;
	}

	const FSeam_CellSnapshot Snapshot = ISeam_TileProviderRead::Execute_GetCellSnapshot(TileProvider, Cell);
	if (!Snapshot.IsKnown())
	{
		return !Rules.bRejectUnknownCells;
	}

	const FGameplayTag TileType = Snapshot.TileTypeTag;

	// Block-list takes precedence.
	if (Rules.BlockedTileTypes.Num() > 0 && TileType.IsValid()
		&& Rules.BlockedTileTypes.HasTag(TileType))
	{
		return false;
	}

	// Allow-list: when non-empty the tile type must match (be a child of) one entry.
	if (Rules.AllowedTileTypes.Num() > 0)
	{
		if (!TileType.IsValid() || !Rules.AllowedTileTypes.HasTag(TileType))
		{
			return false;
		}
	}

	return true;
}

bool ULvl_ProceduralPlacerComponent::TraceSurface(const ULvl_PlacementRuleSet& Rules,
	const FVector& Candidate, FHitResult& OutHit) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Start = Candidate + FVector(0.0, 0.0, Rules.TraceStartHeight);
	const FVector End = Start - FVector(0.0, 0.0, Rules.TraceDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(LvlProceduralPlacerSurfaceTrace), /*bTraceComplex=*/false);
	if (const AActor* Owner = GetOwner())
	{
		Params.AddIgnoredActor(Owner);
	}
	// Ignore actors this placer already spawned so stacking placements do not project onto each other.
	for (const TWeakObjectPtr<AActor>& Weak : SpawnedActors)
	{
		if (AActor* Existing = Weak.Get())
		{
			Params.AddIgnoredActor(Existing);
		}
	}

	return World->LineTraceSingleByChannel(OutHit, Start, End, Rules.SurfaceTraceChannel.GetValue(), Params);
}

bool ULvl_ProceduralPlacerComponent::ValidateAndProject(const ULvl_PlacementRuleSet& Rules,
	const FLvl_PlacementClassChoice& Choice, FRandomStream& Stream, UObject* TileProvider,
	const FVector& Candidate, FTransform& OutTransform) const
{
	// 1) Tile mask (via the read-only grid seam) — done on the candidate XY before projection.
	if (!PassesTileMask(Rules, TileProvider, Candidate))
	{
		return false;
	}

	FVector Location = Candidate;
	FVector SurfaceNormal = FVector::UpVector;

	// 2) Surface projection via a downward trace (the tile seam has no height).
	if (Rules.bProjectToSurface)
	{
		FHitResult Hit;
		const bool bHit = TraceSurface(Rules, Candidate, Hit);
		if (!bHit)
		{
			if (Rules.bRejectOnTraceMiss)
			{
				return false;
			}
			// keep un-projected height
		}
		else
		{
			Location = Hit.ImpactPoint;
			SurfaceNormal = Hit.ImpactNormal.GetSafeNormal();
			if (SurfaceNormal.IsNearlyZero())
			{
				SurfaceNormal = FVector::UpVector;
			}

			// Slope check: angle between the surface normal and world up.
			const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(static_cast<float>(SurfaceNormal | FVector::UpVector), -1.f, 1.f)));
			if (SlopeDeg > Rules.MaxSlopeDegrees)
			{
				return false;
			}
		}
	}

	// 3) Build the final transform: location + per-choice vertical offset, variation yaw/scale, and
	// optional alignment to the surface normal.
	FQuat Rotation = FQuat::Identity;
	if (Rules.bAlignToSurfaceNormal && Rules.bProjectToSurface)
	{
		Rotation = FRotationMatrix::MakeFromZ(SurfaceNormal).ToQuat();
	}
	if (Rules.bRandomYaw)
	{
		const FQuat YawSpin(FVector::UpVector, FMath::DegreesToRadians(Stream.FRandRange(0.f, 360.f)));
		// Apply yaw in the placement's local up so alignment + yaw compose correctly.
		Rotation = Rotation * YawSpin;
	}

	float ScaleMin, ScaleMax;
	Rules.GetClampedScaleRange(ScaleMin, ScaleMax);
	const float Scale = (ScaleMax > ScaleMin) ? Stream.FRandRange(ScaleMin, ScaleMax) : ScaleMin;

	// Vertical offset is applied along the (possibly aligned) up axis.
	const FVector UpAxis = Rotation.GetUpVector();
	Location += UpAxis * Choice.VerticalOffset;

	OutTransform = FTransform(Rotation, Location, FVector(Scale));
	return true;
}

//~ Spawning ----------------------------------------------------------------------------------------

AActor* ULvl_ProceduralPlacerComponent::SpawnEntry(const ULvl_PlacementRuleSet& Rules,
	const FLvl_PlacementClassChoice& Choice, const FTransform& Transform)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Preferred path: route through the core factory (which uses the pool when the recipe permits).
	if (Choice.ActorClassTag.IsValid())
	{
		if (UDP_SpawnFactorySubsystem* Factory = GetFactory())
		{
			if (Factory->IsFactoryRegistered(Choice.ActorClassTag))
			{
				FDP_SpawnParams Params;
				Params.IdentityTag = Choice.ActorClassTag;
				Params.Transform = Transform;
				Params.Owner = GetOwner();
				Params.bAllowPooling = true;
				if (AActor* FromFactory = Factory->Spawn(Choice.ActorClassTag, Params))
				{
					return FromFactory;
				}
			}
		}
	}

	// Fallback path: direct soft-class spawn (no factory registered for the identity tag).
	if (!Choice.ActorClass.IsNull())
	{
		TSubclassOf<AActor> Class = Choice.ActorClass.LoadSynchronous();
		if (Class)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = GetOwner();
			SpawnParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			return World->SpawnActor<AActor>(Class, Transform, SpawnParams);
		}
	}

	UE_LOG(LogDP, Warning, TEXT("Lvl Placer (%s): no factory or class for tag %s; entry skipped."),
		*GetNameSafe(GetOwner()), *Choice.ActorClassTag.ToString());
	return nullptr;
}

AActor* ULvl_ProceduralPlacerComponent::SpawnFromEntry(const FLvl_PlacedEntry& Entry)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Preferred: factory by the stored class tag (pool-aware), using the saved transform verbatim.
	if (UDP_SpawnFactorySubsystem* Factory = GetFactory())
	{
		if (Factory->IsFactoryRegistered(Entry.ActorClassTag))
		{
			FDP_SpawnParams Params;
			Params.IdentityTag = Entry.ActorClassTag;
			Params.Transform = Entry.Transform;
			Params.Owner = GetOwner();
			Params.bAllowPooling = true;
			if (AActor* FromFactory = Factory->Spawn(Entry.ActorClassTag, Params))
			{
				return FromFactory;
			}
		}
	}

	// Fallback: resolve the class through the rule set's class table by matching the stored tag.
	if (RuleSet)
	{
		for (const FLvl_PlacementClassChoice& Choice : RuleSet->ClassChoices)
		{
			if (Choice.ActorClassTag == Entry.ActorClassTag && !Choice.ActorClass.IsNull())
			{
				TSubclassOf<AActor> Class = Choice.ActorClass.LoadSynchronous();
				if (Class)
				{
					FActorSpawnParameters SpawnParams;
					SpawnParams.Owner = GetOwner();
					SpawnParams.SpawnCollisionHandlingOverride =
						ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
					return World->SpawnActor<AActor>(Class, Entry.Transform, SpawnParams);
				}
			}
		}
	}

	UE_LOG(LogDP, Warning, TEXT("Lvl Placer (%s): restore could not resolve class tag %s; entry skipped."),
		*GetNameSafe(GetOwner()), *Entry.ActorClassTag.ToString());
	return nullptr;
}

//~ Helpers -----------------------------------------------------------------------------------------

FGuid ULvl_ProceduralPlacerComponent::MakeDeterministicPlacementId(int32 Seed, int32 CandidateIndex)
{
	// Derive a stable GUID from the seed + candidate index so the SAME pass yields the SAME ids every
	// run (lets save/restore reconcile entries). Spread the two inputs across the four GUID words with
	// distinct mixing constants so adjacent indices do not collide.
	const uint32 A = static_cast<uint32>(Seed);
	const uint32 B = static_cast<uint32>(CandidateIndex);
	return FGuid(
		HashCombine(A, 0x9E3779B9u),
		HashCombine(A ^ 0x85EBCA6Bu, B),
		HashCombine(B, 0xC2B2AE35u),
		HashCombine(A * 2654435761u, B * 40503u));
}

void ULvl_ProceduralPlacerComponent::BroadcastPlacementEvent(const FGameplayTag& Channel, int32 PlacedCount) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	FLvl_PlacementEventPayload Payload;
	Payload.RuleSetTag = Manifest.RuleSetTag;
	Payload.RegionTag = Manifest.RegionTag;
	Payload.RandomSeed = Manifest.RandomSeed;
	Payload.PlacedCount = PlacedCount;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload),
		const_cast<ULvl_ProceduralPlacerComponent*>(this));
}

void ULvl_ProceduralPlacerComponent::DestroySpawnedActors()
{
	for (const TWeakObjectPtr<AActor>& Weak : SpawnedActors)
	{
		if (AActor* Actor = Weak.Get())
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}
	SpawnedActors.Reset();
}
