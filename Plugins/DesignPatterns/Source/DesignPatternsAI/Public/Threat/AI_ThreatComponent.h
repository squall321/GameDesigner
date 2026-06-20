// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Seams/AI_Threatened.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5. Used by the
// reflected combat-payload readers declared below.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "AI_ThreatComponent.generated.h"

/**
 * Fired locally when the top-threat entity changes (on authority and via OnRep on clients).
 * @param NewTopThreat the entity now holding the most threat (invalid if the table emptied).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAI_OnTopThreatChanged, FSeam_EntityId, NewTopThreat);

/**
 * Flat, weak-ref-free bus payload broadcast on DP.Bus.AI.ThreatChanged when the top threat changes.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_ThreatChangedPayload
{
	GENERATED_BODY()

	/** Stable id of the agent whose threat table changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Threat")
	FSeam_EntityId AgentId;

	/** The entity now holding the most threat (invalid if empty). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Threat")
	FSeam_EntityId TopThreat;

	/** The top threat's accumulated threat value. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Threat")
	float TopThreatValue = 0.f;

	FAI_ThreatChangedPayload() = default;
};

/**
 * One row of the aggro table: accumulated, decaying threat from a single source entity.
 *
 * Not replicated as a row (only the resulting top-threat id replicates). Held in a private array on
 * the component; the source is identified by a net/save-stable FSeam_EntityId so the table survives
 * actor churn and never holds a hard UObject ref to a possibly-destroyed attacker.
 */
USTRUCT()
struct DESIGNPATTERNSAI_API FAI_ThreatEntry
{
	GENERATED_BODY()

	/** Stable id of the source crediting this threat. */
	UPROPERTY()
	FSeam_EntityId Source;

	/** Current accumulated threat value (after decay). */
	UPROPERTY()
	float Value = 0.f;

	/** World TimeSeconds at which this entry was last increased (for time-based decay). */
	UPROPERTY()
	double LastAddedTime = 0.0;

	FAI_ThreatEntry() = default;
};

/**
 * Threat TABLE: an aggro table keyed by FSeam_EntityId with time-based decay, IMPLEMENTING the
 * IAI_Threatened seam.
 *
 * DATA-DRIVEN COMBAT INPUT (no Combat include): this component subscribes to the core bus channel
 * DP.Bus.Combat.Damaged and converts each damage event into threat by reading the payload's fields
 * GENERICALLY via UStruct reflection (the field names are EditAnywhere config). It therefore never
 * depends on the Combat module's concrete payload type — only on the agreed tag + a documented field
 * convention. Threat is also addable directly via AddThreat (the seam).
 *
 * REPLICATION: the aggro table itself is authoritative and NOT replicated (it is server bookkeeping);
 * only the resulting TopThreat entity id replicates (FSeam_EntityId replicates fine) so clients can
 * cosmetically react (e.g. UI threat indicators) via OnRep + the message bus. AddThreat and the bus
 * handler are AUTHORITY-ONLY and guarded at the top.
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_ThreatComponent : public UActorComponent, public IAI_Threatened
{
	GENERATED_BODY()

public:
	UAI_ThreatComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin IAI_Threatened
	virtual FSeam_EntityId GetTopThreat() const override { return TopThreat; }
	virtual void AddThreat(FSeam_EntityId Source, float Amount, FGameplayTag ThreatTag) override;
	//~ End IAI_Threatened

	/** BP-friendly read of the current top threat. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Threat")
	FSeam_EntityId BP_GetTopThreat() const { return TopThreat; }

	/** BP-callable, authority-guarded threat add. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Threat")
	void BP_AddThreat(FSeam_EntityId Source, float Amount, FGameplayTag ThreatTag) { AddThreat(Source, Amount, ThreatTag); }

	/** @return the current accumulated threat value for Source (0 if not present). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Threat")
	float GetThreatFor(FSeam_EntityId Source) const;

	/** Clear all threat. AUTHORITY ONLY (no-op on clients). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Threat")
	void ClearThreat();

	/** Remove a single source from the table. AUTHORITY ONLY (no-op on clients). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Threat")
	void RemoveSource(FSeam_EntityId Source);

	/** Broadcast locally when the top-threat entity changes (authority + OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsAI|Threat")
	FAI_OnTopThreatChanged OnTopThreatChanged;

	// ---- Config (tunables; no magic numbers in code) ----

	/** Threat lost per second per entry once decay starts (linear). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat", meta = (ClampMin = "0.0"))
	float DecayPerSecond = 5.f;

	/** Seconds after the last increase before an entry begins to decay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat", meta = (ClampMin = "0.0"))
	float DecayDelaySeconds = 3.f;

	/** Entries at or below this value are pruned from the table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat", meta = (ClampMin = "0.0"))
	float PruneThreshold = 0.5f;

	/** Multiplier applied to raw damage when converting a DP.Bus.Combat.Damaged event into threat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat", meta = (ClampMin = "0.0"))
	float DamageToThreatScale = 1.f;

	/**
	 * When true, this component listens on DP.Bus.Combat.Damaged and converts damage to threat
	 * data-drivenly (see the *FieldName config below). No Combat module dependency is incurred.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat|CombatBridge")
	bool bConvertCombatDamage = true;

	/** When true, top-threat changes are republished on the core bus (DP.Bus.AI.ThreatChanged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat")
	bool bBroadcastOnBus = true;

	/**
	 * Reflected field name on the DP.Bus.Combat.Damaged payload identifying the VICTIM actor
	 * (TWeakObjectPtr<AActor> or AActor*). The event is only converted to threat when this victim
	 * resolves to THIS component's owner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat|CombatBridge")
	FName CombatPayload_VictimField = TEXT("Victim");

	/**
	 * Reflected field name on the payload identifying the INSTIGATOR actor whose entity id is
	 * credited with the resulting threat (TWeakObjectPtr<AActor> or AActor*).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat|CombatBridge")
	FName CombatPayload_InstigatorField = TEXT("Instigator");

	/** Reflected float field name on the payload carrying the damage amount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat|CombatBridge")
	FName CombatPayload_AmountField = TEXT("Amount");

	/** GameplayTag classification stored on threat added from combat damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Threat|CombatBridge")
	FGameplayTag CombatThreatTag;

protected:
	/**
	 * The replicated top-threat entity id — the ONLY replicated field. Clients react via OnRep.
	 * The full aggro table is authoritative server bookkeeping and is never replicated.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TopThreat, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsAI|Threat")
	FSeam_EntityId TopThreat;

	/** Client reaction to a replicated top-threat change: fire the delegate + bus. */
	UFUNCTION()
	void OnRep_TopThreat(FSeam_EntityId PreviousTopThreat);

private:
	/** The authoritative aggro table. Server-only bookkeeping; not replicated. */
	UPROPERTY(Transient)
	TArray<FAI_ThreatEntry> Entries;

	/** Apply time-based decay to all entries, prune the small ones, and recompute the top threat. */
	void DecayAndRecompute(double NowSeconds);

	/** Recompute TopThreat from the table and, if it changed, replicate + notify. */
	void RecomputeTopThreat();

	/** Shared top-threat-change reaction (delegate + optional bus) for authority and OnRep. */
	void HandleTopThreatChanged(FSeam_EntityId Previous, FSeam_EntityId NewTop);

	/** Bus handler for DP.Bus.Combat.Damaged: extract victim/instigator/amount via reflection. */
	void HandleCombatDamaged(const struct FDP_Message& Message);

	/**
	 * Read a TWeakObjectPtr<AActor>/AActor* field named FieldName out of an FInstancedStruct's
	 * payload via reflection. @return the actor, or null when the field is absent/wrong type.
	 */
	static AActor* ReadActorField(const FInstancedStruct& Payload, FName FieldName);

	/**
	 * Read a float/double field named FieldName out of an FInstancedStruct's payload via reflection.
	 * @return true if a numeric field was found; OutValue receives it.
	 */
	static bool ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue);

	/** Resolve a stable entity id for an actor via the identity seam (invalid if none). */
	static FSeam_EntityId GetEntityIdFor(const AActor* Actor);

	/** Resolve this agent's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
