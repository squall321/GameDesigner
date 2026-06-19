// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hit/Combat_HitboxComponent.h"
#include "Hit/Combat_DamageExecution.h"
#include "Health/Combat_HealthComponent.h"

#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UCombat_HitboxComponent::UCombat_HitboxComponent()
{
	// Continuous sweeps need ticking, but only while a window is open (we early-out otherwise).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

bool UCombat_HitboxComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UCombat_HitboxComponent::BeginHitWindow()
{
	bHitWindowActive = true;
	// Clearing the dedupe set at the START of each window lets the same victim be hit once
	// per swing while preventing multi-hits within a single swing.
	HitActorsThisActivation.Reset();
}

void UCombat_HitboxComponent::EndHitWindow()
{
	bHitWindowActive = false;
	HitActorsThisActivation.Reset();
}

void UCombat_HitboxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bHitWindowActive)
	{
		PerformSweep();
	}
}

void UCombat_HitboxComponent::PerformSweep()
{
	// Hit confirmation is server-authoritative. Clients may have the window open for VFX gating
	// but must not confirm damage.
	if (!HasAuthoritySafe())
	{
		return;
	}

	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	const FVector Start = Owner->GetActorLocation();
	const FVector End = Start + Owner->GetActorForwardVector() * SweepReach;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CombatHitboxSweep), /*bTraceComplex*/ false, Owner);
	Params.AddIgnoredActor(Owner);

	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits, Start, End, FQuat::Identity, TraceChannel,
		FCollisionShape::MakeSphere(SweepRadius), Params);

	for (const FHitResult& Hit : Hits)
	{
		AActor* Victim = Hit.GetActor();
		if (Victim)
		{
			ConfirmHit(Victim, Hit.ImpactPoint, Hit.ImpactNormal);
		}
	}
}

void UCombat_HitboxComponent::ConfirmHit(AActor* Victim, const FVector& ImpactPoint, const FVector& ImpactNormal)
{
	// SERVER ONLY — never apply damage on a client.
	if (!HasAuthoritySafe())
	{
		return;
	}

	if (!Victim)
	{
		return;
	}

	// Per-activation dedupe: skip victims already hit in this window.
	const TWeakObjectPtr<AActor> VictimWeak(Victim);
	if (HitActorsThisActivation.Contains(VictimWeak))
	{
		return;
	}

	// Only actors with a health component are valid combat targets.
	UCombat_HealthComponent* VictimHealth = Victim->FindComponentByClass<UCombat_HealthComponent>();
	if (!VictimHealth || VictimHealth->IsDead())
	{
		return;
	}

	HitActorsThisActivation.Add(VictimWeak);

	FCombat_HitResult HitResult;
	HitResult.HitActor = Victim;
	HitResult.Instigator = GetOwner();
	HitResult.ImpactPoint = ImpactPoint;
	HitResult.ImpactNormal = ImpactNormal;
	HitResult.BaseDamage = BaseDamage;
	HitResult.DamageType = DamageType;
	HitResult.SourceTag = SourceTag;

	// Strategy: run the pluggable damage execution if present, else use the raw base damage.
	const float FinalDamage = DamageExecution
		? DamageExecution->CalculateDamage(HitResult)
		: HitResult.BaseDamage;

	VictimHealth->ApplyDamage(FinalDamage, GetOwner());

	OnHitConfirmed.Broadcast(HitResult);

	UE_LOG(LogDP, Verbose, TEXT("[Combat] Hitbox %s confirmed hit on %s for %.1f"),
		*GetNameSafe(GetOwner()), *GetNameSafe(Victim), FinalDamage);
}
