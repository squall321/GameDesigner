// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_FlowFieldSubsystem.h"
#include "Crowd/SimAg_SteeringComponent.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"

void USimAg_FlowFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		DefaultSeparationRadius = FMath::Max(1.f, Settings->DefaultSeparationRadius);
	}

	// Publish ourselves as the flow-field seam fallback. WeakObserved so we never keep a dead world's
	// subsystem alive. A game may override this binding with a richer provider; we forward to it.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(SimAgNativeTags::Service_FlowField, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ false);
	}

	UE_LOG(LogDP, Log, TEXT("SimAg flow-field subsystem initialized (sepRadius=%.0f)."), DefaultSeparationRadius);
}

void USimAg_FlowFieldSubsystem::Deinitialize()
{
	RegisteredAgents.Reset();
	Super::Deinitialize();
}

void USimAg_FlowFieldSubsystem::RegisterAgent(USimAg_SteeringComponent* Steering)
{
	if (!Steering)
	{
		return;
	}
	PruneAgents();
	RegisteredAgents.AddUnique(Steering);
}

void USimAg_FlowFieldSubsystem::UnregisterAgent(USimAg_SteeringComponent* Steering)
{
	RegisteredAgents.RemoveAll([Steering](const TWeakObjectPtr<USimAg_SteeringComponent>& W)
	{
		return !W.IsValid() || W.Get() == Steering;
	});
}

void USimAg_FlowFieldSubsystem::PruneAgents() const
{
	// Const but mutates only the transient registration set, exactly like the service locator's PruneStale.
	TArray<TWeakObjectPtr<USimAg_SteeringComponent>>& Agents =
		const_cast<USimAg_FlowFieldSubsystem*>(this)->RegisteredAgents;
	Agents.RemoveAll([](const TWeakObjectPtr<USimAg_SteeringComponent>& W) { return !W.IsValid(); });
}

TScriptInterface<ISimAg_FlowField> USimAg_FlowFieldSubsystem::ResolveExternalProvider() const
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_FlowField);
		// Only treat it as external if it is NOT us (we registered ourselves as the fallback).
		if (Service && Service != this && Service->Implements<USimAg_FlowField>())
		{
			TScriptInterface<ISimAg_FlowField> Result;
			Result.SetObject(Service);
			Result.SetInterface(Cast<ISimAg_FlowField>(Service));
			return Result;
		}
	}
	return TScriptInterface<ISimAg_FlowField>();
}

//~ ISimAg_FlowField ----------------------------------------------------------------------------

FVector USimAg_FlowFieldSubsystem::SampleFlowDirection_Implementation(const FVector& WorldLocation, const FVector& Goal) const
{
	// Forward to an external provider when one is registered.
	if (TScriptInterface<ISimAg_FlowField> External = ResolveExternalProvider())
	{
		return ISimAg_FlowField::Execute_SampleFlowDirection(External.GetObject(), WorldLocation, Goal);
	}

	// FALLBACK: ask the engine nav system for a path toward the goal and take the first leg's direction.
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

	// No nav data: steer straight at the goal.
	const FVector ToGoal = Goal - WorldLocation;
	return ToGoal.IsNearlyZero() ? FVector::ZeroVector : ToGoal.GetSafeNormal();
}

FVector USimAg_FlowFieldSubsystem::SampleSeparation_Implementation(const FVector& WorldLocation, float QueryRadius) const
{
	if (TScriptInterface<ISimAg_FlowField> External = ResolveExternalProvider())
	{
		return ISimAg_FlowField::Execute_SampleSeparation(External.GetObject(), WorldLocation, QueryRadius);
	}

	const float Radius = QueryRadius > 0.f ? QueryRadius : DefaultSeparationRadius;
	const float RadiusSq = Radius * Radius;

	PruneAgents();

	// FALLBACK separation: sum normalized push-away vectors from registered neighbours within Radius,
	// weighted by closeness (closer agents push harder). Self (the agent at WorldLocation) is excluded.
	FVector Accum = FVector::ZeroVector;
	int32 Neighbours = 0;
	for (const TWeakObjectPtr<USimAg_SteeringComponent>& WeakAgent : RegisteredAgents)
	{
		const USimAg_SteeringComponent* Agent = WeakAgent.Get();
		if (!Agent)
		{
			continue;
		}
		const FVector Other = Agent->GetAgentLocation();
		const FVector Delta = WorldLocation - Other;
		const float DistSq = static_cast<float>(Delta.SizeSquared());
		if (DistSq <= KINDA_SMALL_NUMBER || DistSq > RadiusSq)
		{
			continue; // self or out of range
		}
		const float Dist = FMath::Sqrt(DistSq);
		// Linear falloff to the radius edge; closer => stronger.
		const float Weight = 1.f - (Dist / Radius);
		Accum += (Delta / Dist) * Weight;
		++Neighbours;
	}

	return Neighbours > 0 ? Accum : FVector::ZeroVector;
}

//~ Debug ---------------------------------------------------------------------------------------

FString USimAg_FlowFieldSubsystem::GetDPDebugString_Implementation() const
{
	PruneAgents();
	const bool bExternal = ResolveExternalProvider() ? true : false;
	return FString::Printf(TEXT("SimAg FlowField: %s | agents=%d sepRadius=%.0f"),
		bExternal ? TEXT("external provider") : TEXT("fallback"),
		RegisteredAgents.Num(), DefaultSeparationRadius);
}
