// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Combat_HitReactionComponent.generated.h"

class UCombat_DamagePipelineComponent;
struct FCombat_DamageResult;

/** Fired locally to drive a hit-reaction (anim montage / camera shake) keyed by reaction tag. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnReactionPlayed,
	FGameplayTag, ReactionTag, FVector, ImpactPoint);

/**
 * LOCAL, COSMETIC-ONLY hit reactions. NO replicated state whatsoever.
 *
 * It subscribes to the sibling UCombat_DamagePipelineComponent::OnDamageResolved (server) and/or the
 * DP.Bus.Combat.HitFeedback bus channel (every machine), then plays purely cosmetic feedback:
 *  - a brief hitstop (local time dilation on the owner) scaled by the hit's salience,
 *  - a BlueprintImplementableEvent / delegate for VFX/UI/anim,
 *  - camera shake hooks the project can bind.
 *
 * Because it touches no authoritative state it runs identically on server and clients; clients drive
 * their reactions from the bus message (which is broadcast locally on each machine in response to the
 * replicated health/poise/status deltas), so hit feedback is never gated behind authority.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_HitReactionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_HitReactionComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Broadcast locally whenever a reaction plays. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Reaction")
	FCombat_OnReactionPlayed OnReactionPlayed;

	/** BP hook so designers author bespoke per-reaction cosmetics without C++. */
	UFUNCTION(BlueprintImplementableEvent, Category = "DesignPatternsCombat|Reaction")
	void ReceiveReaction(FGameplayTag ReactionTag, FVector ImpactPoint, bool bWasCritical, bool bWasWeakpoint);

	// ---- Config (content-authored) ----

	/** Global hitstop duration (seconds, real time) applied on a salient hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Reaction", meta = (ClampMin = "0.0"))
	float HitstopSeconds = 0.06f;

	/** Time dilation applied to the owner during hitstop (1 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Reaction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HitstopDilation = 0.05f;

	/** Extra hitstop multiplier on crit/weakpoint/stagger hits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Reaction", meta = (ClampMin = "1.0"))
	float SalientHitstopMultiplier = 1.75f;

	/** If true, also listen on the bus (needed on clients; the pipeline delegate fires only on the server). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Reaction")
	bool bListenOnBus = true;

protected:
	/** Bound to the sibling pipeline's OnDamageResolved (server-side path). */
	UFUNCTION()
	void HandlePipelineResolved(UCombat_DamagePipelineComponent* Component, const FCombat_DamageResult& Result);

private:
	/** Bus subscription handle (so we can unsubscribe in EndPlay). */
	FDP_ListenerHandle BusHandle;

	/** Play the cosmetic reaction for a resolved hit (local only). */
	void PlayReaction(FGameplayTag ReactionTag, const FVector& ImpactPoint, bool bCritical, bool bWeakpoint, bool bStagger);

	/** Apply a brief local hitstop (owner time dilation) and schedule its restore. */
	void ApplyHitstop(bool bSalient);

	/** Timer-restore the owner's time dilation after hitstop. */
	void RestoreTimeDilation();

	/** Subscribe to the DP.Bus.Combat.HitFeedback channel. */
	void SubscribeBus();

	/** Unsubscribe from the bus. */
	void UnsubscribeBus();
};
