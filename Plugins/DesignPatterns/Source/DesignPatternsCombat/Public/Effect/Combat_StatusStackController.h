// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Combat_StatusStackController.generated.h"

class UCombat_StatusEffect;
class UCombat_StatusFamilyEffect;
class UCombat_StatusEffectComponent;
class UCombat_StatusStackController;

/**
 * One replicated per-family stack row. Wrapped in a fast-array so individual family-count changes
 * delta-replicate (a stack tick on one family does not resend the whole table). Plain replicable
 * value types only — NO FInstancedStruct, per the net rules.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_StatusStackEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The family this row counts (e.g. DP.Combat.Status.Family.Bleed). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Stack")
	FGameplayTag FamilyTag;

	/** Current number of active applications in this family. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Stack")
	int32 Count = 0;

	FCombat_StatusStackEntry() = default;
	explicit FCombat_StatusStackEntry(const FGameplayTag& InFamily) : FamilyTag(InFamily), Count(0) {}

	//~ FFastArraySerializerItem client callbacks.
	void PostReplicatedAdd(const struct FCombat_StatusStackArray& InArraySerializer);
	void PostReplicatedChange(const struct FCombat_StatusStackArray& InArraySerializer);
	void PreReplicatedRemove(const struct FCombat_StatusStackArray& InArraySerializer);
};

/**
 * Fast-array of per-family stack rows. NetDeltaSerialize forwards to FastArrayDeltaSerialize so only
 * changed rows cross the wire. The back-pointer is non-replicated and set on both server and client.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_StatusStackArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated per-family stack rows. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Status|Stack")
	TArray<FCombat_StatusStackEntry> Entries;

	/** Non-replicated back-pointer to the owning controller, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UCombat_StatusStackController> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FCombat_StatusStackEntry, FCombat_StatusStackArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the status-stack array. */
template<>
struct TStructOpsTypeTraits<FCombat_StatusStackArray> : public TStructOpsTypeTraitsBase2<FCombat_StatusStackArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Fired (on every machine) when a family's stack count changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCombat_OnFamilyStackChanged,
	UCombat_StatusStackController*, Controller, FGameplayTag, FamilyTag, int32, NewCount);

/**
 * Cross-effect stacking / diminishing-returns / immunity coordinator that sits ALONGSIDE the shipped
 * UCombat_StatusEffectComponent (it does not replace or alter it).
 *
 * FLOW (authority-only): gameplay calls TryApplyFamilyEffect instead of the raw ApplyEffect. The
 * controller:
 *   1. Reads the effect's family/stack policy from its CDO (UCombat_StatusFamilyEffect).
 *   2. Checks family IMMUNITY (an active effect granting immunity to this family blocks the apply).
 *   3. Enforces MaxStacks for the Stack policy.
 *   4. Applies the DIMINISHING-RETURNS duration multiplier based on the current family count
 *      (it sets the spawned instance's Duration before the component starts its timer).
 *   5. Routes to the existing UCombat_StatusEffectComponent::ApplyEffect (which handles same-tag
 *      refresh and the replicated tag set) — so all the shipped behavior is reused.
 *   6. Increments the replicated per-family count (fast-array) and fires OnFamilyStackChanged.
 *
 * REPLICATION: only the per-family stack counts replicate (FCombat_StatusStackArray); the effect
 * instances/timing stay server-side on the status component, exactly as before.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_StatusStackController : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_StatusStackController();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Apply a family-aware status effect with stacking/DR/immunity enforced. AUTHORITY ONLY.
	 * @return the applied/refreshed effect instance, or null if blocked by immunity / max stacks / on a client.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status|Stack")
	UCombat_StatusEffect* TryApplyFamilyEffect(TSubclassOf<UCombat_StatusEffect> EffectClass);

	/** Notify the controller that an effect was removed, so its family count decrements. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Status|Stack")
	void NotifyEffectRemoved(FGameplayTag FamilyTag);

	/** @return current stack count for a family (0 if none). Valid on any machine. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Status|Stack")
	int32 GetFamilyCount(FGameplayTag FamilyTag) const;

	/** @return true if the actor is currently immune to FamilyTag (an active effect grants immunity). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Status|Stack")
	bool IsImmuneToFamily(FGameplayTag FamilyTag) const;

	/** Broadcast when any family's stack count changes (server immediately; clients via the fast-array). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Status|Stack")
	FCombat_OnFamilyStackChanged OnFamilyStackChanged;

	/** Called by the fast-array item callbacks (client) to surface a count change. */
	void HandleFamilyCountReplicated(const FGameplayTag& FamilyTag, int32 NewCount);

protected:
	/** Replicated per-family stack counts. */
	UPROPERTY(Replicated)
	FCombat_StatusStackArray FamilyStacks;

private:
	/** Resolve the sibling status-effect component on the owner (the real applier). May be null. */
	UCombat_StatusEffectComponent* GetStatusComponent() const;

	/** Adjust the replicated count for FamilyTag by Delta (authority), marking the row dirty. */
	void AdjustFamilyCount(const FGameplayTag& FamilyTag, int32 Delta);

	/** Find a row by family (mutable / const). */
	FCombat_StatusStackEntry* FindEntry(const FGameplayTag& FamilyTag);
	const FCombat_StatusStackEntry* FindEntry(const FGameplayTag& FamilyTag) const;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
