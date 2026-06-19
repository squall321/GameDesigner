// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat_StatusEffectComponent.generated.h"

class UCombat_StatusEffect;
class UCombat_StatusEffectComponent;

/**
 * Authority-side runtime record for one active status effect. NOT replicated as a struct — only
 * the aggregated ActiveEffectTags container replicates; clients learn which effects are present
 * from the tags, while the per-instance timing lives on the server.
 */
USTRUCT()
struct FCombat_ActiveStatus
{
	GENERATED_BODY()

	/** The live effect instance (server-owned subobject). */
	UPROPERTY()
	TObjectPtr<UCombat_StatusEffect> Effect = nullptr;

	/** World time (seconds) at which this effect expires. <= now means expired (0 = infinite). */
	float ExpireTime = 0.f;

	/** World time (seconds) of the next OnTick. */
	float NextTickTime = 0.f;

	FCombat_ActiveStatus() = default;
};

/** Fired (on every machine) when an effect tag is added to the active set. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnStatusApplied,
	UCombat_StatusEffectComponent*, Component, FGameplayTag, EffectTag);

/** Fired (on every machine) when an effect tag is removed from the active set. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnStatusRemoved,
	UCombat_StatusEffectComponent*, Component, FGameplayTag, EffectTag);

/**
 * Manages timed buffs/debuffs on an actor.
 *
 * REPLICATION: only ActiveEffectTags (an FGameplayTagContainer) replicates — clients see WHICH
 * effects are active (for UI / cosmetic gating) but the authoritative timing and OnApply/OnTick/
 * OnRemove logic run server-side. SetIsReplicatedByDefault(true) in the ctor; the container is
 * registered in GetLifetimeReplicatedProps with an OnRep that fires applied/removed deltas.
 *
 * AUTHORITY: ApplyEffect / RemoveEffect / RemoveEffectByTag mutate replicated state and run the
 * effect hooks; they are guarded at the top and are no-ops on clients. Durations and tick cadence
 * use world time.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_StatusEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_StatusEffectComponent();

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Apply an effect by class. AUTHORITY ONLY (no-op on clients). Instantiates the effect as a
	 * server-owned subobject (Outer = this), runs OnApply, adds its tag to the replicated set.
	 * If an effect with the same tag is already active it is refreshed (timer reset) instead.
	 * @return the spawned/refreshed effect instance, or null on failure / on a client.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status")
	UCombat_StatusEffect* ApplyEffect(TSubclassOf<UCombat_StatusEffect> EffectClass);

	/** Remove a specific active effect instance. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status")
	void RemoveEffect(UCombat_StatusEffect* Effect);

	/** Remove the first active effect whose EffectTag matches. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status")
	void RemoveEffectByTag(FGameplayTag EffectTag);

	/** @return true if an effect with the given tag is currently active (valid on any machine). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Status")
	bool HasEffectTag(FGameplayTag EffectTag) const { return ActiveEffectTags.HasTag(EffectTag); }

	/** @return a copy of the replicated active-effect tag container. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Status")
	FGameplayTagContainer GetActiveEffectTags() const { return ActiveEffectTags; }

	/** Broadcast when an effect tag becomes active (server immediately; clients via OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Status")
	FCombat_OnStatusApplied OnStatusApplied;

	/** Broadcast when an effect tag is cleared (server immediately; clients via OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Status")
	FCombat_OnStatusRemoved OnStatusRemoved;

protected:
	/**
	 * The ONLY replicated state: the set of active effect tags. Clients observe additions/removals
	 * via OnRep_ActiveEffectTags and fire the matching delegates locally.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveEffectTags, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status")
	FGameplayTagContainer ActiveEffectTags;

	/** Server-only list of live effect instances + their timing. Not replicated. */
	UPROPERTY()
	TArray<FCombat_ActiveStatus> ActiveEffects;

	/** Client reaction to a replicated tag-set change: diff old vs new, fire applied/removed. */
	UFUNCTION()
	void OnRep_ActiveEffectTags(FGameplayTagContainer OldTags);

private:
	/** Server tick: drive OnTick cadence and expire finished effects. */
	void TickEffectsAuthority(float DeltaTime);

	/** Remove the active-effects entry at Index (server), running OnRemove and clearing its tag. */
	void RemoveAt(int32 Index, bool bExpiredNaturally);

	/** Broadcast a DP.Bus.Combat.StatusApplied message via the core bus. */
	void BroadcastStatusApplied(FGameplayTag EffectTag);

	/** Broadcast a DP.Bus.Combat.StatusRemoved message via the core bus. */
	void BroadcastStatusRemoved(FGameplayTag EffectTag);

	/** @return current world time in seconds, or 0 if no world. */
	float GetWorldTimeSeconds() const;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
