// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Defense/Combat_DefenseComponent.h"
#include "Defense/Combat_PoiseComponent.h"
#include "Combat_DeepNativeTags.h"

#include "Action/DPGameplayActionComponent.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/NetSerialization.h"
#include "Net/UnrealNetwork.h"

UCombat_DefenseComponent::UCombat_DefenseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);

	// Defensive defaults so the component is usable without a content pass; designers override.
	GuardNeedTag = CombatDeepNativeTags::Need_Guard;
	PoiseNeedTag = CombatDeepNativeTags::Need_Poise;
}

void UCombat_DefenseComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthoritySafe())
	{
		GuardMeter = MaxGuardMeter;
		bGuardBroken = false;
		DefenseState = ECombat_DefenseState::None;
		LastGuardDrainTime = GetWorldTimeSeconds();
	}
}

void UCombat_DefenseComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCombat_DefenseComponent, GuardMeter);
	DOREPLIFETIME(UCombat_DefenseComponent, DefenseState);
	DOREPLIFETIME(UCombat_DefenseComponent, bGuardBroken);
}

bool UCombat_DefenseComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UCombat_DefenseComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

UDP_GameplayActionComponent* UCombat_DefenseComponent::GetActionComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UDP_GameplayActionComponent>() : nullptr;
}

UCombat_PoiseComponent* UCombat_DefenseComponent::GetPoiseComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UCombat_PoiseComponent>() : nullptr;
}

// ---------------------------------------------------------------------------------------------
//  Client intent -> Server RPCs
// ---------------------------------------------------------------------------------------------

void UCombat_DefenseComponent::RequestBeginBlock()
{
	// Locally-controlled client forwards intent; server re-derives. On standalone/authority this still
	// routes through the server function which executes locally.
	ServerBeginBlock();
}

void UCombat_DefenseComponent::RequestEndBlock()
{
	ServerEndBlock();
}

void UCombat_DefenseComponent::RequestDodge(FVector DirectionLocal)
{
	if (DirectionLocal.IsNearlyZero())
	{
		DirectionLocal = FVector::BackwardVector; // documented fallback
	}
	ServerDodge(DirectionLocal.GetSafeNormal());
}

bool UCombat_DefenseComponent::ServerBeginBlock_Validate() { return true; }
void UCombat_DefenseComponent::ServerBeginBlock_Implementation()
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	// Cannot block while dodging or guard-broken.
	if (DefenseState == ECombat_DefenseState::Dodging || bGuardBroken)
	{
		return;
	}

	BlockStartTime = GetWorldTimeSeconds();
	// A fresh block opens the parry window first; it collapses to plain Blocking after the window.
	SetDefenseStateAuthority(ECombat_DefenseState::Parrying);
}

bool UCombat_DefenseComponent::ServerEndBlock_Validate() { return true; }
void UCombat_DefenseComponent::ServerEndBlock_Implementation()
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	if (DefenseState == ECombat_DefenseState::Blocking || DefenseState == ECombat_DefenseState::Parrying)
	{
		SetDefenseStateAuthority(ECombat_DefenseState::None);
	}
}

bool UCombat_DefenseComponent::ServerDodge_Validate(FVector_NetQuantizeNormal /*DirectionLocal*/) { return true; }
void UCombat_DefenseComponent::ServerDodge_Implementation(FVector_NetQuantizeNormal /*DirectionLocal*/)
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	// Cannot dodge while already dodging.
	if (DefenseState == ECombat_DefenseState::Dodging)
	{
		return;
	}

	DodgeStartTime = GetWorldTimeSeconds();
	SetDefenseStateAuthority(ECombat_DefenseState::Dodging);
	SetIFrameTag(true);
}

// ---------------------------------------------------------------------------------------------
//  Pure query (CONST)
// ---------------------------------------------------------------------------------------------

bool UCombat_DefenseComponent::QueryIncoming(const FCombat_HitResult& Hit, float& OutChipFraction, bool& OutInvulnerable, bool& OutParry) const
{
	OutChipFraction = 1.f;   // 1 == full damage gets through (no mitigation)
	OutInvulnerable = false;
	OutParry = false;

	// Dodge i-frames negate the hit entirely.
	if (DefenseState == ECombat_DefenseState::Dodging && IsWithinDodgeIFrames())
	{
		OutInvulnerable = true;
		return true;
	}

	const bool bBlockish = (DefenseState == ECombat_DefenseState::Blocking || DefenseState == ECombat_DefenseState::Parrying);
	if (!bBlockish || bGuardBroken)
	{
		return false;
	}

	// Facing check: a block only works if the attacker is within the frontal arc.
	if (BlockFacingDot > -1.f)
	{
		const AActor* Owner = GetOwner();
		const AActor* Attacker = Hit.Instigator.Get();
		if (Owner && Attacker)
		{
			const FVector ToAttacker = (Attacker->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
			const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal2D();
			if (FVector::DotProduct(Forward, ToAttacker) < BlockFacingDot)
			{
				return false; // hit came from outside the guard arc
			}
		}
	}

	// Parry window: a perfectly-timed block fully negates and flags a parry.
	if (DefenseState == ECombat_DefenseState::Parrying && IsWithinParryWindow())
	{
		OutParry = true;
		OutChipFraction = 0.f;
		OutInvulnerable = true; // a parried hit deals no HP damage to the defender
		return true;
	}

	// Plain block: only chip damage bleeds through.
	OutChipFraction = FMath::Clamp(BlockChipFraction, 0.f, 1.f);
	return true;
}

// ---------------------------------------------------------------------------------------------
//  Authority mutations (from the pipeline component, post-resolution)
// ---------------------------------------------------------------------------------------------

bool UCombat_DefenseComponent::ConsumeBlock(float GuardCost)
{
	if (!HasAuthoritySafe())
	{
		return false;
	}

	LastGuardDrainTime = GetWorldTimeSeconds();
	GuardMeter = FMath::Max(0.f, GuardMeter - FMath::Max(0.f, GuardCost));

	if (GuardMeter <= 0.f && !bGuardBroken)
	{
		bGuardBroken = true;
		// A broken guard drops the stance so the next hit lands clean.
		SetDefenseStateAuthority(ECombat_DefenseState::None);
		UE_LOG(LogDP, Verbose, TEXT("[Defense] %s guard broken."), *GetNameSafe(GetOwner()));
		return true;
	}

	return false;
}

void UCombat_DefenseComponent::ConsumeParry(AActor* Attacker)
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	// End the parry window (collapse to plain block) and notify listeners so the ATTACKER can be
	// staggered by gameplay code reacting to OnParrySuccess.
	if (DefenseState == ECombat_DefenseState::Parrying)
	{
		SetDefenseStateAuthority(ECombat_DefenseState::Blocking);
	}
	OnParrySuccess.Broadcast(this, Attacker);
	UE_LOG(LogDP, Verbose, TEXT("[Defense] %s parried %s."), *GetNameSafe(GetOwner()), *GetNameSafe(Attacker));
}

// ---------------------------------------------------------------------------------------------
//  Tick / state
// ---------------------------------------------------------------------------------------------

void UCombat_DefenseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HasAuthoritySafe())
	{
		TickAuthority(DeltaTime);
	}
}

void UCombat_DefenseComponent::TickAuthority(float DeltaTime)
{
	const float Now = GetWorldTimeSeconds();

	// Parry window collapse to plain block.
	if (DefenseState == ECombat_DefenseState::Parrying && !IsWithinParryWindow())
	{
		SetDefenseStateAuthority(ECombat_DefenseState::Blocking);
	}

	// Dodge expiry + i-frame tag upkeep.
	if (DefenseState == ECombat_DefenseState::Dodging)
	{
		if (!IsWithinDodgeIFrames())
		{
			SetIFrameTag(false);
		}
		if ((Now - DodgeStartTime) >= DodgeDurationSeconds)
		{
			SetIFrameTag(false);
			SetDefenseStateAuthority(ECombat_DefenseState::None);
		}
	}

	// Guard regen when not actively blocking and after the delay.
	const bool bBlocking = (DefenseState == ECombat_DefenseState::Blocking || DefenseState == ECombat_DefenseState::Parrying);
	if (!bBlocking && GuardRegenPerSecond > 0.f && GuardMeter < MaxGuardMeter && (Now - LastGuardDrainTime) >= GuardRegenDelay)
	{
		GuardMeter = FMath::Min(MaxGuardMeter, GuardMeter + GuardRegenPerSecond * DeltaTime);
		// Recover from guard-break once the meter passes the halfway mark (defensive threshold).
		if (bGuardBroken && GuardMeter >= MaxGuardMeter * 0.5f)
		{
			bGuardBroken = false;
		}
	}
}

bool UCombat_DefenseComponent::IsWithinParryWindow() const
{
	return (GetWorldTimeSeconds() - BlockStartTime) <= ParryWindowSeconds;
}

bool UCombat_DefenseComponent::IsWithinDodgeIFrames() const
{
	return (GetWorldTimeSeconds() - DodgeStartTime) <= (DodgeDurationSeconds * FMath::Clamp(DodgeIFrameFraction, 0.f, 1.f));
}

void UCombat_DefenseComponent::SetIFrameTag(bool bEnabled)
{
	UDP_GameplayActionComponent* Action = GetActionComponent();
	if (!Action)
	{
		return;
	}

	if (bEnabled)
	{
		Action->AddOwnedTag(CombatDeepNativeTags::Status_IFrame);
	}
	else if (Action->GetOwnedTags().HasTag(CombatDeepNativeTags::Status_IFrame))
	{
		Action->RemoveOwnedTag(CombatDeepNativeTags::Status_IFrame);
	}
}

void UCombat_DefenseComponent::SetDefenseStateAuthority(ECombat_DefenseState NewState)
{
	if (DefenseState == NewState)
	{
		return;
	}

	DefenseState = NewState;
	OnDefenseStateChanged.Broadcast(this, NewState);

	// Mirror the stance onto the action component's owned-tags so other systems (UI, AI) can read it.
	if (UDP_GameplayActionComponent* Action = GetActionComponent())
	{
		const bool bBlocking = (NewState == ECombat_DefenseState::Blocking || NewState == ECombat_DefenseState::Parrying);
		if (bBlocking)
		{
			Action->AddOwnedTag(CombatDeepNativeTags::Status_Blocking);
		}
		else if (Action->GetOwnedTags().HasTag(CombatDeepNativeTags::Status_Blocking))
		{
			Action->RemoveOwnedTag(CombatDeepNativeTags::Status_Blocking);
		}
	}
}

void UCombat_DefenseComponent::OnRep_DefenseState(ECombat_DefenseState /*OldState*/)
{
	OnDefenseStateChanged.Broadcast(this, DefenseState);
}

void UCombat_DefenseComponent::OnRep_GuardMeter(float /*OldValue*/)
{
	// Reserved for client guard-bar UI.
}

// ---------------------------------------------------------------------------------------------
//  Need seam (ISeam_NeedProvider)
// ---------------------------------------------------------------------------------------------

float UCombat_DefenseComponent::GetNeedNormalized_Implementation(FGameplayTag NeedTag) const
{
	if (GuardNeedTag.IsValid() && NeedTag == GuardNeedTag)
	{
		return GetGuardNormalized();
	}

	if (PoiseNeedTag.IsValid() && NeedTag == PoiseNeedTag)
	{
		if (const UCombat_PoiseComponent* Poise = GetPoiseComponent())
		{
			return Poise->GetPoiseNormalized();
		}
		return 0.f;
	}

	return 0.f;
}

bool UCombat_DefenseComponent::SupportsNeed_Implementation(FGameplayTag NeedTag) const
{
	if (GuardNeedTag.IsValid() && NeedTag == GuardNeedTag)
	{
		return true;
	}
	// Only claim the poise need if a poise component is actually present to answer it.
	if (PoiseNeedTag.IsValid() && NeedTag == PoiseNeedTag)
	{
		return GetPoiseComponent() != nullptr;
	}
	return false;
}

void UCombat_DefenseComponent::GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const
{
	if (GuardNeedTag.IsValid())
	{
		OutNeeds.AddTag(GuardNeedTag);
	}
	if (PoiseNeedTag.IsValid() && GetPoiseComponent() != nullptr)
	{
		OutNeeds.AddTag(PoiseNeedTag);
	}
}
