// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Resource/USurv_ResourceNodeComponent.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

USurv_ResourceNodeComponent::USurv_ResourceNodeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USurv_ResourceNodeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USurv_ResourceNodeComponent, RemainingAmount);
	DOREPLIFETIME(USurv_ResourceNodeComponent, bDepleted);
}

void USurv_ResourceNodeComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialise the node full. Only the authority seeds replicated state; clients receive it.
	if (HasAuthorityToMutate())
	{
		RemainingAmount = FMath::Max(1, MaxAmount);
		bDepleted = false;
	}
}

void USurv_ResourceNodeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// HARD RULE: clear the timer so it never fires into a torn-down world.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool USurv_ResourceNodeComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

int32 USurv_ResourceNodeComponent::Harvest(int32 RequestedAmount, USurv_ResourceStoreComponent* DepositInto)
{
	if (!HasAuthorityToMutate())
	{
		return 0;
	}
	if (RequestedAmount <= 0 || bDepleted || RemainingAmount <= 0)
	{
		return 0;
	}

	const int32 Harvested = FMath::Min(RequestedAmount, RemainingAmount);
	RemainingAmount -= Harvested;

	if (DepositInto && ResourceTag.IsValid())
	{
		DepositInto->AddResource(ResourceTag, Harvested);
	}

	UE_LOG(LogDP, Verbose, TEXT("[Survival] Harvested %d of %s (%d left)"),
		Harvested, *ResourceTag.ToString(), RemainingAmount);

	OnHarvested.Broadcast(ResourceTag, Harvested);

	if (RemainingAmount <= 0)
	{
		bDepleted = true;
		OnDepleted.Broadcast(ResourceTag);

		if (RespawnSeconds > 0.f)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					RespawnTimerHandle, this, &USurv_ResourceNodeComponent::HandleRespawn,
					RespawnSeconds, /*bLoop*/ false);
			}
		}
	}

	return Harvested;
}

void USurv_ResourceNodeComponent::ForceRespawn()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	HandleRespawn();
}

void USurv_ResourceNodeComponent::HandleRespawn()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	RemainingAmount = FMath::Max(1, MaxAmount);
	bDepleted = false;

	UE_LOG(LogDP, Verbose, TEXT("[Survival] Node %s respawned"), *ResourceTag.ToString());
	OnRespawned.Broadcast(ResourceTag);
}

void USurv_ResourceNodeComponent::OnRep_RemainingAmount()
{
	// Clients surface a harvest-ish event so cosmetic listeners can react to the depleting count.
	OnHarvested.Broadcast(ResourceTag, RemainingAmount);
}

void USurv_ResourceNodeComponent::OnRep_Depleted()
{
	if (bDepleted)
	{
		OnDepleted.Broadcast(ResourceTag);
	}
	else
	{
		OnRespawned.Broadcast(ResourceTag);
	}
}
