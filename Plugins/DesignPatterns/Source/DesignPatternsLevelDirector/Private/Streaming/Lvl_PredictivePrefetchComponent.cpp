// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/Lvl_PredictivePrefetchComponent.h"
#include "Streaming/Lvl_StreamingDirectorSubsystem.h"
#include "Streaming/Lvl_MemoryBudgetWatcherSubsystem.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/MovementComponent.h"

ULvl_PredictivePrefetchComponent::ULvl_PredictivePrefetchComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Per-machine interest source; streaming is a local decision, so never replicate.
	SetIsReplicatedByDefault(false);
}

void ULvl_PredictivePrefetchComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoRegister)
	{
		if (ULvl_StreamingDirectorSubsystem* Director = GetDirector())
		{
			TScriptInterface<ILvl_InterestSource> Self;
			Self.SetObject(this);
			Self.SetInterface(Cast<ILvl_InterestSource>(this));
			Director->RegisterInterestSource(Self);
			bRegistered = true;
		}
	}
}

void ULvl_PredictivePrefetchComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (ULvl_StreamingDirectorSubsystem* Director = GetDirector())
		{
			TScriptInterface<ILvl_InterestSource> Self;
			Self.SetObject(this);
			Self.SetInterface(Cast<ILvl_InterestSource>(this));
			Director->UnregisterInterestSource(Self);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

ULvl_StreamingDirectorSubsystem* ULvl_PredictivePrefetchComponent::GetDirector() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<ULvl_StreamingDirectorSubsystem>(this);
}

ULvl_MemoryBudgetWatcherSubsystem* ULvl_PredictivePrefetchComponent::GetMemoryWatcher() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<ULvl_MemoryBudgetWatcherSubsystem>(this);
}

FVector ULvl_PredictivePrefetchComponent::GetOwnerVelocity() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	// Prefer a movement component's velocity; fall back to the actor's reported velocity.
	if (const UMovementComponent* Move = Owner->FindComponentByClass<UMovementComponent>())
	{
		return Move->Velocity;
	}
	return Owner->GetVelocity();
}

FVector ULvl_PredictivePrefetchComponent::GetInterestLocation_Implementation() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}
	const FVector Base = Owner->GetActorLocation();
	const FVector Lead = GetOwnerVelocity() * FMath::Max(0.f, LeadSeconds);
	const float LeadLen = static_cast<float>(Lead.Size());
	if (LeadLen <= KINDA_SMALL_NUMBER)
	{
		return Base;
	}
	// Clamp the lead distance.
	const FVector ClampedLead = Lead * (FMath::Min(LeadLen, MaxLeadDistance) / LeadLen);
	return Base + ClampedLead;
}

float ULvl_PredictivePrefetchComponent::GetInterestRadius_Implementation() const
{
	// EXTRA radius only (the policy bands already supply the base); proportional to speed.
	const float Speed = static_cast<float>(GetOwnerVelocity().Size());
	float Extra = FMath::Clamp(Speed * FMath::Max(0.f, SpeedRadiusScale), 0.f, MaxExtraRadius);

	// Shrink the extra radius at the SOURCE under memory pressure so the director unloads naturally.
	if (const ULvl_MemoryBudgetWatcherSubsystem* Watcher = GetMemoryWatcher())
	{
		const float Pressure = FMath::Clamp(Watcher->GetCategoryPressure(InterestCategory), 0.f, 1.f);
		Extra *= (1.f - Pressure);
	}
	return FMath::Max(0.f, Extra);
}
