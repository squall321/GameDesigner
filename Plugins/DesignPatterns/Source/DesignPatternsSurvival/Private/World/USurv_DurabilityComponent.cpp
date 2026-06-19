// Copyright DesignPatterns plugin. All Rights Reserved.

#include "World/USurv_DurabilityComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USurv_DurabilityComponent::USurv_DurabilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USurv_DurabilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USurv_DurabilityComponent, CurrentDurability);
}

void USurv_DurabilityComponent::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthorityToMutate())
	{
		CurrentDurability = FMath::Max(1.f, MaxDurability);
		bWasBroken = false;
	}
}

bool USurv_DurabilityComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

float USurv_DurabilityComponent::GetDurabilityNormalized() const
{
	return MaxDurability > 0.f ? FMath::Clamp(CurrentDurability / MaxDurability, 0.f, 1.f) : 0.f;
}

float USurv_DurabilityComponent::ApplyWear(float Amount)
{
	if (!HasAuthorityToMutate())
	{
		return CurrentDurability;
	}
	if (Amount <= 0.f)
	{
		return CurrentDurability;
	}
	CurrentDurability = FMath::Clamp(CurrentDurability - Amount, 0.f, MaxDurability);
	UE_LOG(LogDP, Verbose, TEXT("[Survival] Wear %.1f -> durability %.1f"), Amount, CurrentDurability);
	NotifyChanged();
	return CurrentDurability;
}

float USurv_DurabilityComponent::Repair(float Amount)
{
	if (!HasAuthorityToMutate())
	{
		return CurrentDurability;
	}
	if (Amount <= 0.f)
	{
		return CurrentDurability;
	}
	CurrentDurability = FMath::Clamp(CurrentDurability + Amount, 0.f, MaxDurability);
	NotifyChanged();
	return CurrentDurability;
}

void USurv_DurabilityComponent::NotifyChanged()
{
	OnDurabilityChanged.Broadcast(CurrentDurability);

	const bool bBroken = CurrentDurability <= 0.f;
	if (bBroken && !bWasBroken)
	{
		OnBroken.Broadcast();
	}
	else if (!bBroken && bWasBroken)
	{
		OnRepaired.Broadcast();
	}
	bWasBroken = bBroken;
}

void USurv_DurabilityComponent::OnRep_CurrentDurability()
{
	// Clients run the same change/edge notifications off the replicated value.
	NotifyChanged();
}
