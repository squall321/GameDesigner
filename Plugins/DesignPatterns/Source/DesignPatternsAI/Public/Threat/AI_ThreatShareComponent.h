// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI/Seam_TauntSink.h"
#include "AI_ThreatShareComponent.generated.h"

class IAI_Threatened;
struct FDP_Message;

/**
 * Additive threat EXTENSION that composes the existing UAI_ThreatComponent through the IAI_Threatened
 * seam (AddThreat / GetTopThreat) — it NEVER edits the threat table type. It implements ISeam_TauntSink
 * so combat/abilities can force aggro without depending on this concrete component.
 *
 * Squad propagation: enumerates squadmates via the new IAI_Squad::GetMembers (resolved from
 * DP.Service.AI.Squad), resolves each member's IAI_Threatened, and shares/transfers threat across them.
 *
 * AUTHORITY: every share/taunt/transfer is authority-guarded at the TOP (threat is authoritative gameplay
 * state). Only the forced-target id (TauntForcedTarget) replicates so clients can react cosmetically and
 * the local brain can be biased. It listens DP.Bus.AI.ThreatChanged to react to its own table changes and
 * optionally pushes the top threat into IAI_Brain::SetTargetEntity.
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_ThreatShareComponent : public UActorComponent, public ISeam_TauntSink
{
	GENERATED_BODY()

public:
	UAI_ThreatShareComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_TauntSink
	/** Force this agent to focus Source for Duration seconds. AUTHORITY ONLY. (<= 0 clears.) */
	virtual void ForceTaunt(FSeam_EntityId Source, float Duration) override;
	//~ End ISeam_TauntSink

	/**
	 * Apply a taunt that makes Tauntee focus ForcedTarget for Duration. AUTHORITY ONLY. When Tauntee is the
	 * owner this is equivalent to ForceTaunt; otherwise it resolves Tauntee's ISeam_TauntSink and forwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Threat")
	void ApplyTaunt(FSeam_EntityId Tauntee, FSeam_EntityId ForcedTarget, float Duration);

	/**
	 * Share a fraction of a threat event to every squadmate's threat table, so an attack on one alerts the
	 * squad. AUTHORITY ONLY. @param Source the entity to credit; @param Amount the base amount (squadmates
	 * receive Amount * SquadShareFraction); @param Tag the threat classification.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Threat")
	void ShareThreatToSquad(FSeam_EntityId Source, float Amount, FGameplayTag Tag);

	/**
	 * Transfer a fraction of From's threat to To across the owner's threat table (e.g. on a guard swap).
	 * AUTHORITY ONLY. Reduces From's threat and credits To by the same amount.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Threat")
	void TransferThreat(FSeam_EntityId From, FSeam_EntityId To, float Fraction);

	/** @return the currently forced taunt target (invalid when no taunt active). Safe on any machine. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Threat")
	FSeam_EntityId GetForcedTarget() const { return TauntForcedTarget; }

	// ---- Config (tunables; no magic gameplay numbers in code) ----

	/** Fraction of a shared threat event each squadmate receives (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Threat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SquadShareFraction = 0.25f;

	/**
	 * When true the component pushes the effective top threat (forced target if tainting, else the table's
	 * top threat) into the owner's IAI_Brain::SetTargetEntity so the brain actually engages it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Threat")
	bool bDriveBrainTarget = true;

	/**
	 * How often (seconds) the component re-evaluates the forced-taunt expiry and re-drives the brain target.
	 * Coordination cadence, not a gameplay rate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Threat", meta = (ClampMin = "0.05"))
	float EvaluateInterval = 0.25f;

protected:
	/** The forced taunt target; replicated so clients/UI and the brain bias can read it. */
	UPROPERTY(ReplicatedUsing = OnRep_ForcedTarget, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatterns|AI|Threat")
	FSeam_EntityId TauntForcedTarget;

	/** Client reaction to a replicated forced-target change. */
	UFUNCTION()
	void OnRep_ForcedTarget(FSeam_EntityId PreviousTarget);

private:
	/** World TimeSeconds at which the active taunt expires (authority bookkeeping; not replicated). */
	double TauntExpiryTime = 0.0;

	/** Accumulator for the evaluate cadence. */
	float EvaluateAccumulator = 0.f;

	/** Resolve the owner's own IAI_Threatened (its threat table), or null. */
	IAI_Threatened* ResolveOwnThreat() const;

	/** Resolve an actor's IAI_Threatened by stable entity id (rebuilds via world iteration), or null. */
	TScriptInterface<IAI_Threatened> ResolveThreatById(const FSeam_EntityId& EntityId) const;

	/** Resolve an actor's ISeam_TauntSink by stable entity id, or null. */
	TScriptInterface<ISeam_TauntSink> ResolveTauntSinkById(const FSeam_EntityId& EntityId) const;

	/** Append the owner's squadmate ids (excluding the owner) via IAI_Squad::GetMembers. */
	void GetSquadmates(TArray<FSeam_EntityId>& OutMembers) const;

	/** Push the effective top threat into the owner's brain (authority only). */
	void DriveBrainTarget();

	/** Bus handler for DP.Bus.AI.ThreatChanged: re-drive the brain target when our own table changes. */
	void HandleThreatChanged(const FDP_Message& Message);

	/** Resolve the owner's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** Resolve any actor in the world carrying EntityId via the identity seam, or null. */
	AActor* ResolveActorById(const FSeam_EntityId& EntityId) const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
