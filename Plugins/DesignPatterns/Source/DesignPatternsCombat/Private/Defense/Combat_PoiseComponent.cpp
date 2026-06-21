// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Defense/Combat_PoiseComponent.h"
#include "Combat_DeepNativeTags.h"

#include "Action/DPGameplayActionComponent.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UCombat_PoiseComponent::UCombat_PoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UCombat_PoiseComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthoritySafe())
	{
		Poise = MaxPoise;
		bStaggered = false;
		StaggerEndTime = 0.f;
		LastPoiseDamageTime = GetWorldTimeSeconds();
	}
}

void UCombat_PoiseComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCombat_PoiseComponent, Poise);
	DOREPLIFETIME(UCombat_PoiseComponent, bStaggered);
}

bool UCombat_PoiseComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UCombat_PoiseComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

UDP_GameplayActionComponent* UCombat_PoiseComponent::GetActionComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UDP_GameplayActionComponent>() : nullptr;
}

bool UCombat_PoiseComponent::HasHyperarmor() const
{
	const UDP_GameplayActionComponent* Action = GetActionComponent();
	return Action && Action->GetOwnedTags().HasTag(CombatDeepNativeTags::Status_Hyperarmor);
}

void UCombat_PoiseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HasAuthoritySafe())
	{
		TickAuthority(DeltaTime);
	}
}

void UCombat_PoiseComponent::TickAuthority(float DeltaTime)
{
	const float Now = GetWorldTimeSeconds();

	// Stagger recovery: once the window elapses, snap poise back to full and clear the state.
	if (bStaggered)
	{
		if (Now >= StaggerEndTime)
		{
			ResetPoise();
		}
		return; // No regen while staggered.
	}

	// Regen after the delay since the last poise hit.
	if (RegenPerSecond > 0.f && Poise < MaxPoise && (Now - LastPoiseDamageTime) >= RegenDelay)
	{
		Poise = FMath::Min(MaxPoise, Poise + RegenPerSecond * DeltaTime);
	}
}

bool UCombat_PoiseComponent::ApplyPoiseDamage(float Amount)
{
	// AUTHORITY GUARD at the very top — replicated state.
	if (!HasAuthoritySafe())
	{
		return false;
	}

	if (Amount <= 0.f || bStaggered)
	{
		return false;
	}

	LastPoiseDamageTime = GetWorldTimeSeconds();
	Poise = FMath::Max(0.f, Poise - Amount);

	// Hyperarmor absorbs the break: meter still drains but cannot trigger a stagger.
	if (Poise <= 0.f && !HasHyperarmor())
	{
		bStaggered = true;
		StaggerEndTime = GetWorldTimeSeconds() + FMath::Max(0.f, StaggerRecoverySeconds);

		if (UDP_GameplayActionComponent* Action = GetActionComponent())
		{
			Action->AddOwnedTag(CombatDeepNativeTags::Status_Staggered);
		}

		OnPoiseStateChanged.Broadcast(this, /*bBroken*/ true);
		UE_LOG(LogDP, Verbose, TEXT("[Poise] %s staggered."), *GetNameSafe(GetOwner()));
		return true;
	}

	return false;
}

void UCombat_PoiseComponent::ResetPoise()
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	const bool bWasStaggered = bStaggered;
	Poise = MaxPoise;
	bStaggered = false;
	StaggerEndTime = 0.f;

	if (bWasStaggered)
	{
		if (UDP_GameplayActionComponent* Action = GetActionComponent())
		{
			Action->RemoveOwnedTag(CombatDeepNativeTags::Status_Staggered);
		}
		OnPoiseStateChanged.Broadcast(this, /*bBroken*/ false);
	}
}

void UCombat_PoiseComponent::OnRep_Poise(float /*OldPoise*/)
{
	// Reserved for client-side poise-bar UI; no behavioural reaction needed.
}

void UCombat_PoiseComponent::OnRep_Staggered()
{
	// Mirror the authority broadcast on clients so hit-react / stagger anims play locally.
	OnPoiseStateChanged.Broadcast(this, bStaggered);
}
