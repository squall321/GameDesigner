// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Action/DPGameplayActionLite.h"
#include "Action/DPGameplayActionInterface.h"
#include "DPGameplayActionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnActionActivated, FGameplayTag, ActionTag, FDP_ActionSpecHandle, Handle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnActionEnded, FGameplayTag, ActionTag, bool, bWasCancelled);

/**
 * A GAS-free analog of UAbilitySystemComponent. Grants lightweight actions (UDP_GameplayActionLite),
 * gates them through CanActivate + a world-time cooldown, and runs them.
 *
 * Replication policy (HARD RULE 9): only the *granted-action tag list* and the owner's loose tag
 * container replicate — the small, must-be-synced surface. Action UObjects, cooldown timers and
 * activation payloads are deliberately NOT replicated; activation is server-authoritative and
 * clients learn results through gameplay state / cosmetic events. The full GAS bridge (prediction,
 * attribute sets, gameplay effects) lives in the OPT-IN DesignPatternsGAS module, not here.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNS_API UDP_GameplayActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDP_GameplayActionComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// ---- Granting ----

	/**
	 * Grant an action by class. Instantiates the action (Outer = this) and records a spec.
	 * Returns the spec handle (invalid on failure). Server-authoritative; call on the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	FDP_ActionSpecHandle GrantAction(TSubclassOf<UDP_GameplayActionLite> ActionClass);

	/** Remove a granted action by handle. Returns true if a grant was removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool RemoveAction(FDP_ActionSpecHandle Handle);

	/** Remove the first granted action matching the tag. Returns true if removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool RemoveActionByTag(FGameplayTag ActionTag);

	// ---- Activation ----

	/**
	 * Try to activate a granted action by handle. Runs CanActivate + cooldown gate, then Activate.
	 * On success applies cooldown and broadcasts OnActionActivated. Returns true if it activated.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool ActivateAction(FDP_ActionSpecHandle Handle, const FDP_ActionActivationData& Data);

	/** Try to activate the first granted action with the given tag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool ActivateActionByTag(FGameplayTag ActionTag, const FDP_ActionActivationData& Data);

	/** End an active action by handle (cancel or completion). Broadcasts OnActionEnded. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	void EndAction(FDP_ActionSpecHandle Handle, const FDP_ActionActivationData& Data, bool bWasCancelled);

	// ---- Queries ----

	/** True if any granted action has the given tag. Works on clients via the replicated tag list. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool HasActionWithTag(FGameplayTag ActionTag) const;

	/** True if the action identified by Handle is currently off cooldown and ready to activate. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	bool IsActionReady(FDP_ActionSpecHandle Handle) const;

	/** Remaining cooldown seconds for the action (0 if ready or unknown). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	float GetActionCooldownRemaining(FDP_ActionSpecHandle Handle) const;

	/** The replicated set of tags for all currently-granted actions. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	const FGameplayTagContainer& GetGrantedActionTags() const { return GrantedActionTags; }

	// ---- Loose owned tags (for activation gating) ----

	/** Add a loose gameplay tag to the owner (e.g. Stunned). Replicated. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	void AddOwnedTag(FGameplayTag Tag);

	/** Remove a loose gameplay tag from the owner. Replicated. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	void RemoveOwnedTag(FGameplayTag Tag);

	/** The owner's loose tag container, consulted by action CanActivate gating. Replicated. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Action")
	const FGameplayTagContainer& GetOwnedTags() const { return OwnedTags; }

	// ---- Events ----

	/** Broadcast (locally) when an action activates. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Action")
	FDP_OnActionActivated OnActionActivated;

	/** Broadcast (locally) when an action ends. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Action")
	FDP_OnActionEnded OnActionEnded;

private:
	/** All granted action specs (server-authoritative; not directly replicated). */
	UPROPERTY()
	TArray<FDP_ActionSpec> Specs;

	/**
	 * Replicated set of granted-action tags. This is the only action-grant state that crosses
	 * the wire, letting clients answer HasActionWithTag for UI without replicating the actions.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_GrantedActionTags)
	FGameplayTagContainer GrantedActionTags;

	/** Replicated loose owner tags used for activation gating. */
	UPROPERTY(Replicated)
	FGameplayTagContainer OwnedTags;

	/**
	 * True only on the network authority (server, or any standalone game). Granting/removing
	 * actions and editing owner tags mutate server-authoritative / replicated state, so every
	 * such entry point guards on this first.
	 */
	bool HasAuthorityToMutate() const;

	/** Find a spec by handle (mutable / const). */
	FDP_ActionSpec* FindSpec(const FDP_ActionSpecHandle& Handle);
	const FDP_ActionSpec* FindSpec(const FDP_ActionSpecHandle& Handle) const;

	/** Rebuild GrantedActionTags from Specs and flag it dirty for replication. */
	void RefreshGrantedActionTags();

	/** Current world time in seconds (0 if no world). */
	float GetWorldTimeSeconds() const;

	UFUNCTION()
	void OnRep_GrantedActionTags();
};
