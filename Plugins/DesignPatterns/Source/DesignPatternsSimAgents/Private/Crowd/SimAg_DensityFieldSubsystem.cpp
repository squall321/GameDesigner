// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_DensityFieldSubsystem.h"
#include "Crowd/SimAg_SteeringComponent.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void USimAg_DensityFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		DefaultSeparationRadius = FMath::Max(1.f, Settings->DefaultSeparationRadius);
		BinSize = DefaultSeparationRadius;
	}
	RegisteredServiceTag = SimAgNativeTags::Service_FlowField;

	// Register as the flow-field provider WITH override so the shipped fallback forwards to us. Last writer
	// under Service_FlowField wins (documented); the shipped subsystem registered without override, so our
	// override takes precedence regardless of init order, and its ResolveExternalProvider then finds us.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(RegisteredServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}

	UE_LOG(LogDP, Log, TEXT("SimAg density field initialized (binSize=%.0f)."), BinSize);
}

void USimAg_DensityFieldSubsystem::Deinitialize()
{
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			// Only unregister if WE are still the bound provider, so we don't clobber a later override.
			if (Locator->ResolveService(RegisteredServiceTag) == this)
			{
				Locator->UnregisterService(RegisteredServiceTag);
			}
		}
	}
	Bins.Reset();
	Super::Deinitialize();
}

FIntVector USimAg_DensityFieldSubsystem::CellOf(const FVector& Pos) const
{
	const float Safe = FMath::Max(1.f, BinSize);
	return FIntVector(
		FMath::FloorToInt(static_cast<float>(Pos.X) / Safe),
		FMath::FloorToInt(static_cast<float>(Pos.Y) / Safe),
		0);
}

void USimAg_DensityFieldSubsystem::ReportAgent(const FVector& Pos, const FVector& Velocity, float Radius)
{
	FSimAg_DensitySample Sample;
	Sample.Position = Pos;
	Sample.Velocity = Velocity;
	Sample.Radius = FMath::Max(1.f, Radius);
	Bins.FindOrAdd(CellOf(Pos)).Add(Sample);
	++SampleCount;
}

void USimAg_DensityFieldSubsystem::FlushFrame()
{
	Bins.Reset();
	SampleCount = 0;
}

FVector USimAg_DensityFieldSubsystem::SampleFlowDirection_Implementation(const FVector& WorldLocation, const FVector& Goal) const
{
	// Density field is a SEPARATION provider, not a field generator: defer the "where to go" vector to the
	// engine nav system (same fallback the shipped subsystem uses) so we don't reinvent pathing.
	UWorld* World = GetWorld();
	if (UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr)
	{
		if (UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, WorldLocation, Goal))
		{
			const TArray<FVector>& Points = Path->PathPoints;
			if (Path->IsValid() && Points.Num() >= 2)
			{
				const FVector Leg = Points[1] - WorldLocation;
				if (!Leg.IsNearlyZero())
				{
					return Leg.GetSafeNormal();
				}
			}
		}
	}
	const FVector ToGoal = Goal - WorldLocation;
	return ToGoal.IsNearlyZero() ? FVector::ZeroVector : ToGoal.GetSafeNormal();
}

FVector USimAg_DensityFieldSubsystem::SampleSeparation_Implementation(const FVector& WorldLocation, float QueryRadius) const
{
	const float Radius = QueryRadius > 0.f ? QueryRadius : DefaultSeparationRadius;
	const float RadiusSq = Radius * Radius;

	// If no agent reported its kinematics this frame (no external tick driver feeds ReportAgent), fall back
	// to a self-sufficient world scan of registered steering components so the density field is useful
	// out-of-the-box WITHOUT any steering edits. When samples ARE reported, the binned fast path is used.
	if (Bins.Num() == 0)
	{
		return SampleSeparationFromWorld(WorldLocation, Radius);
	}

	// Walk only the 3x3 bins around the query cell (density-aware, O(local)).
	const FIntVector Centre = CellOf(WorldLocation);
	FVector Accum = FVector::ZeroVector;
	int32 Neighbours = 0;

	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			const FIntVector Cell(Centre.X + dx, Centre.Y + dy, 0);
			const TArray<FSimAg_DensitySample>* Cellmates = Bins.Find(Cell);
			if (!Cellmates)
			{
				continue;
			}
			for (const FSimAg_DensitySample& Other : *Cellmates)
			{
				FVector Delta = WorldLocation - Other.Position;
				Delta.Z = 0.f;
				const float DistSq = static_cast<float>(Delta.SizeSquared());
				if (DistSq <= KINDA_SMALL_NUMBER || DistSq > RadiusSq)
				{
					continue; // self or out of range
				}
				const float Dist = FMath::Sqrt(DistSq);
				// Proximity weight: closer pushes harder (linear falloff to the radius).
				float Weight = 1.f - (Dist / Radius);
				// RVO-flavoured closing-velocity boost: if the neighbour is moving toward us, push sooner.
				const FVector ToSelf = Delta / Dist;
				const float Closing = static_cast<float>(FVector::DotProduct(Other.Velocity, -ToSelf));
				if (Closing > 0.f)
				{
					// Normalize the closing term against the neighbour radius so it stays bounded.
					Weight += FMath::Min(1.f, Closing / FMath::Max(1.f, Other.Radius));
				}
				Accum += ToSelf * Weight;
				++Neighbours;
			}
		}
	}

	return Neighbours > 0 ? Accum : FVector::ZeroVector;
}

FVector USimAg_DensityFieldSubsystem::SampleSeparationFromWorld(const FVector& WorldLocation, float Radius) const
{
	const float RadiusSq = Radius * Radius;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	FVector Accum = FVector::ZeroVector;
	int32 Neighbours = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		const USimAg_SteeringComponent* Steering = Actor->FindComponentByClass<USimAg_SteeringComponent>();
		if (!Steering)
		{
			continue;
		}
		FVector Delta = WorldLocation - Steering->GetAgentLocation();
		Delta.Z = 0.f;
		const float DistSq = static_cast<float>(Delta.SizeSquared());
		if (DistSq <= KINDA_SMALL_NUMBER || DistSq > RadiusSq)
		{
			continue; // self or out of range
		}
		const float Dist = FMath::Sqrt(DistSq);
		const float Weight = 1.f - (Dist / Radius);
		Accum += (Delta / Dist) * Weight;
		++Neighbours;
	}
	return Neighbours > 0 ? Accum : FVector::ZeroVector;
}

FString USimAg_DensityFieldSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimAg DensityField: samples=%d bins=%d binSize=%.0f"),
		SampleCount, Bins.Num(), BinSize);
}
