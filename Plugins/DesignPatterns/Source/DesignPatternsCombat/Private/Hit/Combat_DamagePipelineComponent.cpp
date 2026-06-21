// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hit/Combat_DamagePipelineComponent.h"
#include "Hit/Combat_HitboxComponent.h"
#include "Pipeline/Combat_PipelineDamageExecution.h"
#include "Health/Combat_HealthComponent.h"
#include "Effect/Combat_StatusEffectComponent.h"
#include "Effect/Combat_StatusStackController.h"
#include "Effect/Combat_StatusEffect.h"
#include "Defense/Combat_DefenseComponent.h"
#include "Defense/Combat_PoiseComponent.h"
#include "Combat_DeepNativeTags.h"

#include "Combat/Seam_DamageReactor.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

UCombat_DamagePipelineComponent::UCombat_DamagePipelineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCombat_DamagePipelineComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UCombat_DamagePipelineComponent::BeginPlay()
{
	Super::BeginPlay();

	// All side effects are authoritative; only bind on the server/standalone.
	if (HasAuthoritySafe())
	{
		BindHitboxes();
	}
}

void UCombat_DamagePipelineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<UCombat_HitboxComponent>& WeakBox : BoundHitboxes)
	{
		if (UCombat_HitboxComponent* Box = WeakBox.Get())
		{
			Box->OnHitConfirmed.RemoveDynamic(this, &UCombat_DamagePipelineComponent::HandleHitConfirmed);
		}
	}
	BoundHitboxes.Reset();

	Super::EndPlay(EndPlayReason);
}

void UCombat_DamagePipelineComponent::BindHitboxes()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<UCombat_HitboxComponent*> Hitboxes;
	Owner->GetComponents<UCombat_HitboxComponent>(Hitboxes);
	for (UCombat_HitboxComponent* Box : Hitboxes)
	{
		if (Box && !Box->OnHitConfirmed.IsAlreadyBound(this, &UCombat_DamagePipelineComponent::HandleHitConfirmed))
		{
			Box->OnHitConfirmed.AddDynamic(this, &UCombat_DamagePipelineComponent::HandleHitConfirmed);
			BoundHitboxes.Add(Box);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[Pipeline] %s bound %d hitbox(es)."), *GetNameSafe(Owner), BoundHitboxes.Num());
}

UCombat_PipelineDamageExecution* UCombat_DamagePipelineComponent::ResolveExecution(UCombat_HitboxComponent* Hitbox) const
{
	// Prefer the hitbox's own execution if it is a pipeline execution (the opt-in path).
	if (Hitbox)
	{
		if (UCombat_PipelineDamageExecution* PipeExec = Cast<UCombat_PipelineDamageExecution>(Hitbox->DamageExecution))
		{
			return PipeExec;
		}
	}
	// Else fall back to our configured execution (may be null -> base damage used).
	return FallbackExecution;
}

void UCombat_DamagePipelineComponent::HandleHitConfirmed(const FCombat_HitResult& Hit)
{
	// AUTHORITY GUARD at the very TOP — all side effects below mutate authoritative/replicated state.
	if (!HasAuthoritySafe())
	{
		return;
	}

	AActor* Victim = Hit.HitActor.Get();
	if (!Victim)
	{
		return;
	}

	// Build the final context via the PURE pass. We cannot know which hitbox fired from the delegate
	// payload alone, so resolve an execution: prefer the fallback (configured on this component); the
	// per-hitbox execution already ran when the hitbox applied its own base damage, but the deep layer
	// re-derives the full context (poise/DoT/flags) here from the same pure logic.
	FCombat_DamageContext Context = FCombat_DamageContext::FromHit(Hit);

	if (UCombat_PipelineDamageExecution* Exec = FallbackExecution)
	{
		Exec->BuildContextAndRun(Hit, Context);
	}
	else
	{
		// No execution: still run the read-only mitigation so blocks/i-frames/weakpoints register.
		// We reuse a transient execution's mitigation by constructing a minimal one is overkill; instead
		// apply the same defensive reads inline through the victim components is already encapsulated in
		// the execution — so without an execution we leave Context at base (documented fallback).
	}

	// Derive the immutable result and cache it for the cosmetic reaction component.
	FCombat_DamageResult Result = FCombat_DamageResult::FromContext(Context);

	// Poise can convert the result into a stagger; compute it before snapshotting flags into feedback.
	ApplyPoise(Victim, Context);
	Result.bStaggered = Context.bStaggered;
	if (Context.bStaggered)
	{
		Result.ReactionTag = CombatDeepNativeTags::Reaction_Stagger;
	}

	LastResult = Result;

	// ----- Side effects (each re-guards authority at its own top) -----
	ApplyInstantDamage(Victim, Result);
	ApplyDot(Victim, Result);
	ApplyDefenseConsumption(Victim, Context);
	ApplyKnockback(Victim, Result);
	NotifyReactors(Victim, Result);
	BroadcastHitFeedback(Result);

	OnDamageResolved.Broadcast(this, Result);
}

void UCombat_DamagePipelineComponent::ApplyInstantDamage(AActor* Victim, const FCombat_DamageResult& Result) const
{
	if (UCombat_HealthComponent* Health = Victim->FindComponentByClass<UCombat_HealthComponent>())
	{
		Health->ApplyDamageFromResult(Result);
	}
}

void UCombat_DamagePipelineComponent::ApplyDot(AActor* Victim, const FCombat_DamageResult& Result) const
{
	if (Result.DotDamage <= 0.f || !DotEffectClass)
	{
		return;
	}

	// Prefer routing through the stack controller (family/DR/immunity); else the raw status component.
	if (UCombat_StatusStackController* Stack = Victim->FindComponentByClass<UCombat_StatusStackController>())
	{
		if (UCombat_StatusEffect* Applied = Stack->TryApplyFamilyEffect(DotEffectClass))
		{
			// Seed the DoT's magnitude from the converted damage so the burn does what was converted.
			Applied->Magnitude = Result.DotDamage;
		}
		return;
	}

	if (UCombat_StatusEffectComponent* Status = Victim->FindComponentByClass<UCombat_StatusEffectComponent>())
	{
		if (UCombat_StatusEffect* Applied = Status->ApplyEffect(DotEffectClass))
		{
			Applied->Magnitude = Result.DotDamage;
		}
	}
}

void UCombat_DamagePipelineComponent::ApplyPoise(AActor* Victim, FCombat_DamageContext& Context) const
{
	if (Context.PoiseDamage <= 0.f)
	{
		return;
	}

	if (UCombat_PoiseComponent* Poise = Victim->FindComponentByClass<UCombat_PoiseComponent>())
	{
		const bool bBroke = Poise->ApplyPoiseDamage(Context.PoiseDamage);
		Context.bStaggered = bBroke;
	}
}

void UCombat_DamagePipelineComponent::ApplyDefenseConsumption(AActor* Victim, const FCombat_DamageContext& Context) const
{
	UCombat_DefenseComponent* Defense = Victim->FindComponentByClass<UCombat_DefenseComponent>();
	if (!Defense)
	{
		return;
	}

	if (Context.bWasParried)
	{
		Defense->ConsumeParry(Context.Hit.Instigator.Get());
	}
	else if (Context.bWasBlocked)
	{
		// Guard cost scales with the pre-mitigation damage so heavy hits drain more guard.
		const float Cost = (Context.BaseDamage + Context.FlatBonus) * FMath::Max(0.f, GuardCostPerDamage);
		Defense->ConsumeBlock(Cost);
	}
}

void UCombat_DamagePipelineComponent::ApplyKnockback(AActor* Victim, const FCombat_DamageResult& Result) const
{
	if (Result.FinalDamage <= 0.f || KnockbackPerDamage <= 0.f)
	{
		return;
	}

	const AActor* Attacker = Result.Instigator.Get();
	FVector Dir = FVector::ZeroVector;
	if (Attacker)
	{
		Dir = (Victim->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal();
	}
	if (Dir.IsNearlyZero())
	{
		Dir = -Victim->GetActorForwardVector(); // documented fallback: push straight back
	}

	FVector Impulse = Dir * (Result.FinalDamage * KnockbackPerDamage);
	Impulse.Z += Result.FinalDamage * KnockbackPerDamage * FMath::Max(0.f, KnockbackUpFraction);

	// Prefer a character's movement launch (replicates cleanly); else a primitive impulse.
	if (ACharacter* Char = Cast<ACharacter>(Victim))
	{
		Char->LaunchCharacter(Impulse, /*bXYOverride*/ false, /*bZOverride*/ false);
	}
	else if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Victim->GetRootComponent()))
	{
		if (Root->IsSimulatingPhysics())
		{
			Root->AddImpulse(Impulse, NAME_None, /*bVelChange*/ true);
		}
	}
}

void UCombat_DamagePipelineComponent::NotifyReactors(AActor* Victim, const FCombat_DamageResult& Result) const
{
	AActor* Instigator = Result.Instigator.Get();

	// Notify the victim actor itself if it implements the reactor seam.
	if (Victim->GetClass()->ImplementsInterface(USeam_DamageReactor::StaticClass()))
	{
		ISeam_DamageReactor::Execute_OnDamageResolved(Victim, Instigator, Result.FinalDamage, Result.DamageChannel, Result.ReactionTag);
	}

	// Also notify reactor components on the victim (AI threat, audio, anim) so several systems can
	// subscribe without all living on the actor class itself.
	TArray<UActorComponent*> Components = Victim->GetComponentsByInterface(USeam_DamageReactor::StaticClass());
	for (UActorComponent* Comp : Components)
	{
		if (Comp)
		{
			ISeam_DamageReactor::Execute_OnDamageResolved(Comp, Instigator, Result.FinalDamage, Result.DamageChannel, Result.ReactionTag);
		}
	}
}

void UCombat_DamagePipelineComponent::BroadcastHitFeedback(const FCombat_DamageResult& Result) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	// Reuse the shipped FCombat_StatusMessage shape would lose detail; broadcast a primitive-only
	// FCombat_HitResult-derived payload via the existing hit struct (net-safe, no FInstancedStruct on
	// the wire — the bus is local). We send the original hit so listeners can place VFX.
	FCombat_HitResult Feedback;
	Feedback.HitActor = Result.Victim;
	Feedback.Instigator = Result.Instigator;
	Feedback.ImpactPoint = Result.ImpactPoint;
	Feedback.BaseDamage = Result.FinalDamage;
	Feedback.SourceTag = Result.ReactionTag;

	const FInstancedStruct Payload = FInstancedStruct::Make(Feedback);
	Bus->BroadcastPayload(CombatDeepNativeTags::Bus_Combat_HitFeedback, Payload, Result.Instigator.Get());

	if (Result.bStaggered)
	{
		Bus->BroadcastPayload(CombatDeepNativeTags::Bus_Combat_Staggered, Payload, Result.Victim.Get());
	}
}
