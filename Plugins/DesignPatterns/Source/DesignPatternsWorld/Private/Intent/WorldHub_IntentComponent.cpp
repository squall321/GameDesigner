// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Intent/WorldHub_IntentComponent.h"
#include "Faction/WorldHub_FactionMatrixComponent.h"
#include "History/WorldHub_HistorySubsystem.h"
#include "WorldHub_NativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPLog.h"

UWorldHub_IntentComponent::UWorldHub_IntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Player-owned: must be replicated so its Server RPCs route from the owning client to the server.
	SetIsReplicatedByDefault(true);
}

UWorldHub_FactionMatrixComponent* UWorldHub_IntentComponent::ResolveFactionMatrix() const
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		return Locator->Resolve<UWorldHub_FactionMatrixComponent>(WorldHubNativeTags::Service_WorldHub_FactionMatrix);
	}
	return nullptr;
}

//~ Standing change ----------------------------------------------------------------------------

bool UWorldHub_IntentComponent::Server_RequestStandingChange_Validate(FGameplayTag A, FGameplayTag B, float /*Delta*/)
{
	// Reject structurally-invalid requests outright (faction tags must be present).
	return A.IsValid() && B.IsValid();
}

void UWorldHub_IntentComponent::Server_RequestStandingChange_Implementation(FGameplayTag A, FGameplayTag B, float Delta)
{
	// Server-side: clamp the per-request magnitude, then defer to the authority component which
	// re-clamps to the matrix bounds and guards authority itself.
	float ClampedDelta = Delta;
	if (MaxStandingDeltaPerRequest > 0.0f)
	{
		ClampedDelta = FMath::Clamp(Delta, -MaxStandingDeltaPerRequest, MaxStandingDeltaPerRequest);
	}

	if (UWorldHub_FactionMatrixComponent* Matrix = ResolveFactionMatrix())
	{
		Matrix->Authority_AdjustStanding(A, B, ClampedDelta);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[WorldHub] Intent: no faction matrix resolved for standing change."));
	}
}

//~ Rewind -------------------------------------------------------------------------------------

bool UWorldHub_IntentComponent::Server_RequestRewindToCheckpoint_Validate(FGameplayTag CheckpointLabel)
{
	return CheckpointLabel.IsValid();
}

void UWorldHub_IntentComponent::Server_RequestRewindToCheckpoint_Implementation(FGameplayTag CheckpointLabel)
{
	// Resolve the SERVER-side history subsystem; RewindToCheckpoint is authority-gated inside it.
	if (UWorldHub_HistorySubsystem* History =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_HistorySubsystem>(this))
	{
		History->RewindToCheckpoint(CheckpointLabel);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[WorldHub] Intent: no history subsystem resolved for rewind."));
	}
}
