// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_SquadTacticsComponent.generated.h"

class IAI_Squad;
class IDP_BlackboardProvider;

/**
 * Per-member group-tactics EXECUTION. Reads its assigned WORLD formation slot ONLY via
 * IAI_Squad::GetFormationSlot (already anchor-composed) — it never reads the carrier/anchor and never
 * includes AI_SquadCarrier.h. Squad-wide coordination (e.g. bounding overwatch turn-taking) uses ONLY the
 * per-squad World-hub blackboard the subsystem maintains, so members coordinate without direct refs.
 *
 * It writes the resolved slot / tactic move target into the agent blackboard the FSM and movement read.
 * It adds NO new replicated state — slots replicate on the carrier; tactics are derived locally on the
 * authority and pushed to the (server-authoritative) blackboard.
 *
 * AUTHORITY: tactic execution + blackboard writes are server-only (guarded at the TOP).
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_SquadTacticsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAI_SquadTacticsComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Set which squad this member belongs to (used to resolve its slot + the squad hub blackboard). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	void SetSquad(FGuid InSquadId);

	/** @return the squad this member belongs to (invalid if solo). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Squad")
	FGuid GetSquad() const { return SquadId; }

	/**
	 * Execute a group tactic for this member. AUTHORITY ONLY. Resolves the move target appropriate to the
	 * tactic (formation slot, suppress position holding the slot, or an advance offset toward the squad
	 * objective) and writes it to BlackboardKey_SlotLocation. Broadcasts DP.Bus.AI.Tactic.
	 *
	 * The tactic vocabulary is data (designer tags under AI.Tactic); this component recognizes the generic
	 * Advance / Suppress / HoldSlot behaviours and treats any unknown tactic as HoldSlot.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	void ExecuteTactic(FGameplayTag TacticTag);

	/**
	 * Resolve this member's assigned absolute formation slot transform via IAI_Squad::GetFormationSlot
	 * (from DP.Service.AI.Squad). Identity if not in a squad / no slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	FTransform ResolveAssignedSlot() const;

	// ---- Config (tunables; no magic gameplay numbers in code) ----

	/** Blackboard key the resolved slot / tactic destination is written under (vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Squad")
	FName BlackboardKey_SlotLocation = TEXT("AI.SlotLocation");

	/**
	 * Hub-blackboard key (per-squad) used to coordinate bounding-overwatch turn order. The component reads
	 * this shared int to decide if it is its turn to advance vs. hold-and-suppress.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Squad")
	FName HubKey_BoundingTurn = TEXT("BoundingTurn");

	/** Seconds between automatic bounding-overwatch turn advances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Squad", meta = (ClampMin = "0.1"))
	float BoundingOverwatchInterval = 3.f;

	/** Distance (world units) an advancing member pushes its slot toward the squad objective per bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Squad", meta = (ClampMin = "1.0"))
	float AdvanceStepDistance = 300.f;

	/** When true, tactic execution is republished on the core bus (DP.Bus.AI.Tactic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Squad")
	bool bBroadcastOnBus = true;

private:
	/** The squad this member belongs to. */
	UPROPERTY(Transient)
	FGuid SquadId;

	/** The last executed tactic (so Tick can re-run it as the squad/anchor moves). */
	UPROPERTY(Transient)
	FGameplayTag ActiveTactic;

	/** Accumulator for the bounding-overwatch turn cadence. */
	float BoundingAccumulator = 0.f;

	/** Resolve IAI_Squad from the service locator (DP.Service.AI.Squad), or null. */
	TScriptInterface<IAI_Squad> ResolveSquadSeam() const;

	/** Resolve the owner's blackboard provider. */
	IDP_BlackboardProvider* ResolveBlackboardProvider() const;

	/** Read the per-squad hub blackboard int for HubKey_BoundingTurn (defaulting to 0 when absent). */
	int32 ReadBoundingTurn() const;

	/** Advance the per-squad hub blackboard bounding turn (authority only). */
	void AdvanceBoundingTurn();

	/** Resolve the owner's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** Broadcast a flat tactic payload on the core bus. */
	void BroadcastTacticOnBus(FGameplayTag TacticTag) const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
