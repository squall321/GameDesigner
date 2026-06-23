// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Health/Combat_HealthComponent.h"
#include "Hit/Combat_HitTypes.h"
#include "Pipeline/Combat_DamageContext.h"
#include "Combat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"
#include "Combat/Seam_DamageReactor.h"

// FInstancedStruct is used directly here (FInstancedStruct::Make for the bus payload). Include it
// explicitly rather than leaning on a transitive include; version-gated for the 5.3-5.5 band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UCombat_HealthComponent::UCombat_HealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// REPLICATION CONTRACT: declare this component replicated before any replicated property
	// can travel. Without this the Health/MaxHealth UPROPERTYs would never reach clients.
	SetIsReplicatedByDefault(true);
}

void UCombat_HealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Authority establishes the initial values; clients receive them via replication.
	if (HasAuthoritySafe())
	{
		MaxHealth = FMath::Max(1.f, DefaultMaxHealth);
		Health = MaxHealth;
		bIsDead = false;
	}
}

void UCombat_HealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombat_HealthComponent, Health);
	DOREPLIFETIME(UCombat_HealthComponent, MaxHealth);
	DOREPLIFETIME(UCombat_HealthComponent, bIsDead);
	DOREPLIFETIME(UCombat_HealthComponent, LastInstigator);
}

bool UCombat_HealthComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UCombat_HealthComponent::ApplyDamage(float DamageAmount, AActor* Instigator)
{
	// AUTHORITY GUARD: never mutate replicated state on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0.f;
	}

	if (bIsDead || DamageAmount <= 0.f)
	{
		return 0.f;
	}

	const float OldHealth = Health;
	Health = FMath::Clamp(Health - DamageAmount, 0.f, MaxHealth);
	const float Applied = OldHealth - Health;
	LastInstigator = Instigator;

	// Fire locally on the server (OnRep handles the clients).
	OnDamaged.Broadcast(this, Health, -Applied, Instigator);

	UE_LOG(LogDP, Verbose, TEXT("[Combat] %s took %.1f damage -> %.1f/%.1f"),
		*GetNameSafe(GetOwner()), Applied, Health, MaxHealth);

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		HandleDeath(Instigator);
	}

	return Applied;
}

float UCombat_HealthComponent::ApplyDamageFromResult(const FCombat_DamageResult& Result)
{
	// AUTHORITY GUARD at the top — delegates to ApplyDamage which also re-guards, but we early-out
	// here too so the (FinalDamage - DotDamage) arithmetic never runs needlessly on clients.
	if (!HasAuthoritySafe())
	{
		return 0.f;
	}

	// The DoT portion is applied separately by the pipeline through the status component; the instant
	// portion is what hits HP now. Clamp to non-negative as a defensive guard.
	const float InstantDamage = FMath::Max(0.f, Result.FinalDamage - Result.DotDamage);
	if (InstantDamage <= 0.f)
	{
		return 0.f;
	}

	return ApplyDamage(InstantDamage, Result.Instigator.Get());
}

float UCombat_HealthComponent::Heal(float HealAmount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0.f;
	}

	if (bIsDead || HealAmount <= 0.f)
	{
		return 0.f;
	}

	const float OldHealth = Health;
	Health = FMath::Clamp(Health + HealAmount, 0.f, MaxHealth);
	const float Restored = Health - OldHealth;

	if (Restored > 0.f)
	{
		OnDamaged.Broadcast(this, Health, Restored, nullptr);
	}
	return Restored;
}

void UCombat_HealthComponent::Kill(AActor* Killer)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (bIsDead)
	{
		return;
	}

	const float OldHealth = Health;
	Health = 0.f;
	LastInstigator = Killer;
	bIsDead = true;

	OnDamaged.Broadcast(this, Health, -OldHealth, Killer);
	HandleDeath(Killer);
}

void UCombat_HealthComponent::Revive(float HealthFraction)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!bIsDead)
	{
		return;
	}

	bIsDead = false;
	const float Fraction = FMath::Clamp(HealthFraction, KINDA_SMALL_NUMBER, 1.f);
	Health = FMath::Clamp(MaxHealth * Fraction, 1.f, MaxHealth);
	LastInstigator = nullptr;

	OnDamaged.Broadcast(this, Health, Health, nullptr);
}

void UCombat_HealthComponent::SetMaxHealth(float NewMaxHealth, bool bRescaleCurrent)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	const float OldMax = MaxHealth;
	MaxHealth = FMath::Max(1.f, NewMaxHealth);

	if (bRescaleCurrent && OldMax > 0.f)
	{
		Health = FMath::Clamp(Health * (MaxHealth / OldMax), 0.f, MaxHealth);
	}
	else
	{
		Health = FMath::Clamp(Health, 0.f, MaxHealth);
	}
}

void UCombat_HealthComponent::HandleDeath(AActor* Killer)
{
	OnDeath.Broadcast(this, Killer);
	BroadcastDeathMessage(Killer);

	// Notify defeat reactors (audio, VFX, quests, AI) via the shared seam — mirrors the damage
	// pipeline's NotifyReactors() fan-out. HandleDeath runs exactly once per death (gated by bIsDead),
	// so OnDefeated cannot double-fire.
	if (AActor* Owner = GetOwner())
	{
		if (Owner->GetClass()->ImplementsInterface(USeam_DamageReactor::StaticClass()))
		{
			ISeam_DamageReactor::Execute_OnDefeated(Owner, Killer);
		}

		TArray<UActorComponent*> Components = Owner->GetComponentsByInterface(USeam_DamageReactor::StaticClass());
		for (UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				ISeam_DamageReactor::Execute_OnDefeated(Comp, Killer);
			}
		}
	}

	UE_LOG(LogDP, Log, TEXT("[Combat] %s died (killer: %s)"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Killer));
}

void UCombat_HealthComponent::BroadcastDeathMessage(AActor* Killer)
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FCombat_DeathMessage Death;
	Death.Victim = GetOwner();
	Death.Killer = Killer;

	const FInstancedStruct Payload = FInstancedStruct::Make(Death);
	Bus->BroadcastPayload(CombatNativeTags::Bus_Combat_Death, Payload, GetOwner());
}

void UCombat_HealthComponent::OnRep_Health(float OldHealth)
{
	// Clients derive the signed delta from the replicated value and re-fire the delegate
	// locally so UI/feedback bound on the client still updates.
	const float Delta = Health - OldHealth;
	if (!FMath::IsNearlyZero(Delta))
	{
		OnDamaged.Broadcast(this, Health, Delta, LastInstigator.Get());
	}
}

void UCombat_HealthComponent::OnRep_MaxHealth(float OldMaxHealth)
{
	// No delegate of its own; health-bar widgets typically read GetHealthPercent() each frame
	// or rebind on OnDamaged. Logged for diagnostics only.
	UE_LOG(LogDP, Verbose, TEXT("[Combat] %s MaxHealth replicated %.1f -> %.1f"),
		*GetNameSafe(GetOwner()), OldMaxHealth, MaxHealth);
}

void UCombat_HealthComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		// Death observed on the client: fire OnDeath + broadcast the local bus message.
		HandleDeath(LastInstigator.Get());
	}
}
