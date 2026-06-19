// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "USurv_ResourceNodeComponent.generated.h"

class USurv_ResourceStoreComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnHarvested, FGameplayTag, ResourceTag, int32, AmountHarvested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnDepleted, FGameplayTag, ResourceTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnRespawned, FGameplayTag, ResourceTag);

/**
 * A harvestable resource node (tree, ore vein, bush...) attached to an actor.
 *
 * Replicated so clients see the remaining amount for cosmetic state (e.g. shrink a bush). Harvest
 * is AUTHORITY-ONLY; the remaining amount and depleted flag replicate. When depleted, the node
 * schedules a respawn on the world FTimerManager and CLEARS the timer in EndPlay so it never fires
 * into a torn-down world.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_ResourceNodeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USurv_ResourceNodeComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Harvest up to RequestedAmount from this node, optionally depositing into an inventory store.
	 * AUTHORITY-ONLY: no-op (returns 0) on clients. Returns the amount actually harvested (clamped
	 * to what remains). Depletes the node and schedules respawn when the remaining amount hits 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Resource")
	int32 Harvest(int32 RequestedAmount, USurv_ResourceStoreComponent* DepositInto);

	/** Remaining harvestable amount. Safe on clients (reads replicated state). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Resource")
	int32 GetRemaining() const { return RemainingAmount; }

	/** True if currently depleted (remaining == 0 and awaiting respawn). Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Resource")
	bool IsDepleted() const { return bDepleted; }

	/** Force an immediate refill to MaxAmount. AUTHORITY-ONLY. Cancels any pending respawn. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Resource")
	void ForceRespawn();

	/** The resource produced by this node (e.g. Surv.Resource.Wood). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Resource")
	FGameplayTag ResourceTag;

	/** Full amount the node holds when fresh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Resource", meta = (ClampMin = "1"))
	int32 MaxAmount = 10;

	/** Seconds after depletion before the node refills to MaxAmount. <= 0 means never respawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Resource", meta = (ClampMin = "0.0"))
	float RespawnSeconds = 30.f;

	/** Fired (server + clients via OnRep) when the node yields resources. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Resource")
	FSurv_OnHarvested OnHarvested;

	/** Fired when the node reaches zero remaining. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Resource")
	FSurv_OnDepleted OnDepleted;

	/** Fired when a depleted node refills. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Resource")
	FSurv_OnRespawned OnRespawned;

protected:
	/** Replicated remaining amount. */
	UPROPERTY(ReplicatedUsing = OnRep_RemainingAmount)
	int32 RemainingAmount = 0;

	/** Replicated depleted flag, so clients can suppress harvest cosmetics while empty. */
	UPROPERTY(ReplicatedUsing = OnRep_Depleted)
	bool bDepleted = false;

	/** Handle for the pending respawn timer (server only). Cleared in EndPlay / ForceRespawn. */
	FTimerHandle RespawnTimerHandle;

	UFUNCTION()
	void OnRep_RemainingAmount();

	UFUNCTION()
	void OnRep_Depleted();

	/** Timer callback (server): refill to MaxAmount and broadcast OnRespawned. */
	void HandleRespawn();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;
};
