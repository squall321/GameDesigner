// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Engine/NetSerialization.h"
#include "Needs/Seam_NeedProvider.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_DefenseComponent.generated.h"

class UCombat_DefenseComponent;
class UDP_GameplayActionComponent;
class UCombat_PoiseComponent;

/** Active defensive stance, replicated so clients gate block/parry/dodge VFX. */
UENUM(BlueprintType)
enum class ECombat_DefenseState : uint8
{
	/** No active defense. */
	None,
	/** Holding a block (guard up). */
	Blocking,
	/** Inside the short parry window at the start of a block. */
	Parrying,
	/** Mid-dodge (typically also has the i-frame owned-tag for part of the roll). */
	Dodging
};

/** Fired (on every machine) when the defense state changes (block start/end, parry, dodge). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnDefenseStateChanged,
	UCombat_DefenseComponent*, Component, ECombat_DefenseState, NewState);

/** Fired (on every machine) when a parry successfully catches an incoming hit. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnParrySuccess,
	UCombat_DefenseComponent*, Component, AActor*, Attacker);

/**
 * Block / parry / dodge with a guard meter — the player-owned defensive carrier.
 *
 * NET DESIGN:
 *  - GuardMeter and DefenseState replicate (OnRep) so every machine sees the guard.
 *  - CLIENT INTENT flows through Server...WithValidation RPCs on this PLAYER-OWNED component; the
 *    server re-derives the result. Block/parry/dodge are NEVER set directly by a client.
 *  - QueryIncoming is CONST and side-effect-free — the pure UCombat_PipelineDamageExecution calls it
 *    to learn the chip fraction / invulnerability for a prospective hit. The authority MUTATIONS
 *    (drain the meter, consume a parry, end a dodge) are invoked from the authority-side
 *    UCombat_DamagePipelineComponent AFTER the hit resolves — not from the execution.
 *
 * NEED SEAM: implements ISeam_NeedProvider, answering DP.Combat.Need.Guard (normalized guard meter)
 * and forwarding DP.Combat.Need.Poise to the sibling poise component, so a single provider on the
 * actor surfaces both combat meters generically (resolving the stamina/guard need overlap).
 *
 * I-FRAMES: dodge adds the shared DP.Combat.Status.IFrame owned-tag to the action component; the
 * actual damage nullification is enforced by UCombat_IFrameAwareDamageExecution / QueryIncoming.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_DefenseComponent : public UActorComponent, public ISeam_NeedProvider
{
	GENERATED_BODY()

public:
	UCombat_DefenseComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Client intent (Server RPCs; the locally-controlled client calls these) ----

	/** Request a block. Routed to the server with validation; the server starts the parry window. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	void RequestBeginBlock();

	/** Request to stop blocking. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	void RequestEndBlock();

	/** Request a dodge in DirectionLocal (normalized; defaults to backward). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	void RequestDodge(FVector DirectionLocal);

	// ---- Pure query for the damage execution (CONST, side-effect free, runs anywhere) ----

	/**
	 * Evaluate how this defense would affect an incoming hit, WITHOUT mutating anything.
	 * @param Hit          the prospective hit (used for facing checks vs the attacker).
	 * @param OutChipFraction fraction of damage that still gets through a block in [0,1] (1 = no block).
	 * @param OutInvulnerable true if a dodge/i-frame would fully negate the hit.
	 * @param OutParry        true if the hit lands inside the active parry window.
	 * @return true if any defense applies (block/parry/dodge active and facing the attacker).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	bool QueryIncoming(const FCombat_HitResult& Hit, float& OutChipFraction, bool& OutInvulnerable, bool& OutParry) const;

	// ---- Authority mutations (called from the pipeline component AFTER a hit resolves) ----

	/** Drain the guard meter by a blocked hit's cost. AUTHORITY ONLY. Returns true if guard broke. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	bool ConsumeBlock(float GuardCost);

	/** Consume the active parry against Attacker (ends the window, fires OnParrySuccess). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Defense")
	void ConsumeParry(AActor* Attacker);

	// ---- Queries ----

	/** @return current guard meter. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Defense")
	float GetGuardMeter() const { return GuardMeter; }

	/** @return GuardMeter / MaxGuardMeter in [0,1]. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Defense")
	float GetGuardNormalized() const { return MaxGuardMeter > 0.f ? FMath::Clamp(GuardMeter / MaxGuardMeter, 0.f, 1.f) : 0.f; }

	/** @return the current defense state. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Defense")
	ECombat_DefenseState GetDefenseState() const { return DefenseState; }

	/** @return true if guard is currently broken (meter empty; blocks ineffective until it recovers). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Defense")
	bool IsGuardBroken() const { return bGuardBroken; }

	// ---- Events ----

	/** Broadcast when the defense state changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Defense")
	FCombat_OnDefenseStateChanged OnDefenseStateChanged;

	/** Broadcast when a parry succeeds. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Defense")
	FCombat_OnParrySuccess OnParrySuccess;

	// ---- Need seam (ISeam_NeedProvider) ----

	virtual float GetNeedNormalized_Implementation(FGameplayTag NeedTag) const override;
	virtual bool SupportsNeed_Implementation(FGameplayTag NeedTag) const override;
	virtual void GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const override;

	// ---- Config (content-authored) ----

	/** Maximum guard meter; starts here and recovers to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "1.0"))
	float MaxGuardMeter = 100.f;

	/** Guard recovered per second once not blocking and after the recovery delay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0"))
	float GuardRegenPerSecond = 20.f;

	/** Seconds after the last block hit before guard regen resumes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0"))
	float GuardRegenDelay = 2.f;

	/** Fraction of damage that bleeds through a successful block (chip), in [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlockChipFraction = 0.15f;

	/** Length of the parry window at the start of a block, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0"))
	float ParryWindowSeconds = 0.25f;

	/** Length of the dodge (and its i-frame window), in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0"))
	float DodgeDurationSeconds = 0.4f;

	/** Fraction of the dodge (from its start) during which i-frames are active, in [0,1]. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DodgeIFrameFraction = 0.6f;

	/**
	 * Max dot(forward, toAttacker) past which a block counts as "facing" the attacker. -1 disables the
	 * facing check (block from any angle). Defaults to a frontal arc.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float BlockFacingDot = 0.f;

	/** Need tag answered for the normalized guard meter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense")
	FGameplayTag GuardNeedTag;

	/** Need tag forwarded to the sibling poise component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense")
	FGameplayTag PoiseNeedTag;

protected:
	/** Replicated guard meter. */
	UPROPERTY(ReplicatedUsing = OnRep_GuardMeter, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense")
	float GuardMeter = 100.f;

	/** Replicated defense state. */
	UPROPERTY(ReplicatedUsing = OnRep_DefenseState, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense")
	ECombat_DefenseState DefenseState = ECombat_DefenseState::None;

	/** Replicated guard-broken flag (blocks ineffective until guard recovers above the unbreak threshold). */
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Defense")
	bool bGuardBroken = false;

	/** OnRep: fire OnDefenseStateChanged on clients. */
	UFUNCTION()
	void OnRep_DefenseState(ECombat_DefenseState OldState);

	/** OnRep: reserved for guard-bar UI. */
	UFUNCTION()
	void OnRep_GuardMeter(float OldValue);

	// ---- Server RPCs (validated client intent) ----

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerBeginBlock();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEndBlock();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDodge(FVector_NetQuantizeNormal DirectionLocal);

private:
	/** World time (authority) the current block began, for the parry-window check. */
	float BlockStartTime = 0.f;

	/** World time (authority) the current dodge began, for i-frame / dodge-end timing. */
	float DodgeStartTime = 0.f;

	/** World time (authority) of the last guard drain, for the regen delay. */
	float LastGuardDrainTime = 0.f;

	/** Set the replicated state (authority) and broadcast the change. */
	void SetDefenseStateAuthority(ECombat_DefenseState NewState);

	/** Authority per-tick: parry-window expiry, dodge expiry, guard regen, i-frame tag upkeep. */
	void TickAuthority(float DeltaTime);

	/** @return true if currently inside the parry window (authority/local check). */
	bool IsWithinParryWindow() const;

	/** @return true if currently inside the dodge i-frame window. */
	bool IsWithinDodgeIFrames() const;

	/** Add/remove the shared i-frame owned-tag on the action component. */
	void SetIFrameTag(bool bEnabled);

	/** Resolve the owner's action component (owned tags). May be null. */
	UDP_GameplayActionComponent* GetActionComponent() const;

	/** Resolve the sibling poise component (for the forwarded poise need). May be null. */
	UCombat_PoiseComponent* GetPoiseComponent() const;

	/** @return current world time in seconds, or 0 if no world. */
	float GetWorldTimeSeconds() const;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
