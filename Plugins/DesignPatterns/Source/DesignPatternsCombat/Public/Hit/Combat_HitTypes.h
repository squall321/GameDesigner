// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/HitResult.h"
#include "Combat_HitTypes.generated.h"

/**
 * Broad damage classification used by status effects, resistances and damage execution.
 * Projects can ignore most of these or extend behaviour by reading the type in a
 * UCombat_DamageExecution subclass.
 */
UENUM(BlueprintType)
enum class ECombat_DamageType : uint8
{
	/** Generic untyped damage. */
	Generic,
	/** Melee / physical impact damage. */
	Physical,
	/** Fire/burn damage (often applied over time). */
	Fire,
	/** Frost/cold damage (often applies slow). */
	Frost,
	/** Lightning/shock damage. */
	Lightning,
	/** Poison damage (often applied over time). */
	Poison,
	/** True/unmitigated damage that bypasses resistances. */
	True
};

/**
 * One server-confirmed combat hit. Produced by UCombat_HitboxComponent during a sweep
 * and consumed by UCombat_DamageExecution / UCombat_HealthComponent.
 *
 * This is a plain data struct (not replicated as-is); the authoritative server builds it,
 * applies damage, and the resulting Health change replicates instead.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_HitResult
{
	GENERATED_BODY()

	/** Actor that was hit (the victim). Weak: a destroyed victim must not keep this alive. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	TWeakObjectPtr<AActor> HitActor;

	/** Actor that caused the hit (the attacker / hitbox owner). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	TWeakObjectPtr<AActor> Instigator;

	/** World-space impact point of the sweep that produced this hit. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	FVector ImpactPoint = FVector::ZeroVector;

	/** World-space impact normal at the hit location. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	FVector ImpactNormal = FVector::ZeroVector;

	/** Base (pre-execution) damage carried by the hitbox activation that produced this hit. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	float BaseDamage = 0.f;

	/** Damage classification for resistances / status interactions. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	ECombat_DamageType DamageType = ECombat_DamageType::Generic;

	/** Optional source tag (e.g. the attack/ability tag) for analytics and gating. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Hit")
	FGameplayTag SourceTag;

	FCombat_HitResult() = default;

	/** True if the victim actor is still alive (valid weak pointer). */
	bool IsValid() const { return HitActor.IsValid(); }
};

/**
 * Replicated-locally combat message payload carried over the core message bus when an
 * actor dies. Listeners (UI, scoring, AI) subscribe on the DP.Bus channel hierarchy.
 *
 * NOTE: this is NOT network-replicated by the bus (the bus is local). It is broadcast on
 * each machine in reaction to already-replicated Health state crossing zero.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_DeathMessage
{
	GENERATED_BODY()

	/** The actor that died. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Message")
	TWeakObjectPtr<AActor> Victim;

	/** The actor credited with the kill, if known. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Message")
	TWeakObjectPtr<AActor> Killer;

	FCombat_DeathMessage() = default;
};

/**
 * Bus payload broadcast (locally) when a status effect is applied to an actor.
 * Broadcast on the DP.Bus.Combat.StatusApplied channel.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_StatusMessage
{
	GENERATED_BODY()

	/** The actor the effect was applied to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Message")
	TWeakObjectPtr<AActor> Target;

	/** Identity tag of the applied effect. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Message")
	FGameplayTag EffectTag;

	FCombat_StatusMessage() = default;
};
