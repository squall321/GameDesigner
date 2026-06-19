// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Replication/FNet_RepQuantized.h"
#include "UNet_AuthorityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnActionConfirmed, FGameplayTag, ActionTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNet_OnActionCounterChanged, FGameplayTag, ActionTag, int32, NewCount);

/**
 * Teaching component that demonstrates the canonical multiplayer authority flow used throughout
 * this plugin: Server RPC -> validate (server) -> apply authority-only -> replicate -> multicast
 * cosmetic feedback.
 *
 * It tracks a small bounded per-action counter (how many times each requested action tag has been
 * granted by the server) and replicates it bandwidth-light via FNet_RepInt. The replicated
 * surface is intentionally tiny (HARD RULE 9): only the last-confirmed action tag and its counter.
 *
 * Authoritative entry points (RequestAction) route through a Server RPC so a client can *ask* but
 * never *apply*; the server validates, mutates replicated state, and multicasts a cosmetic event.
 * Every direct mutator is additionally authority-guarded at its top (HARD RULE 6) so the component
 * is safe even when called outside the RPC path.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_AuthorityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_AuthorityComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Public API (callable anywhere; safely routes through the server) ----

	/**
	 * Request that the server perform ActionTag for this actor. Safe to call from a client: it
	 * forwards to the Server RPC. On the server it applies immediately. This is the single entry
	 * point gameplay code should use.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Authority")
	void RequestAction(FGameplayTag ActionTag);

	/** How many times ActionTag has been confirmed by the server (replicated, exact). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Authority")
	int32 GetActionCount(FGameplayTag ActionTag) const;

	/** The most recently confirmed action tag (replicated). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Authority")
	FGameplayTag GetLastConfirmedAction() const { return LastConfirmedAction; }

	/**
	 * Designer validation hook. Override in Blueprint/native to add game-specific rules (cooldowns,
	 * resource costs, owned-tag gating). Runs ON THE SERVER inside the RPC's apply step. Default
	 * implementation accepts any valid tag. This is the BlueprintNativeEvent designer seam.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Net|Authority")
	bool CanServerApplyAction(FGameplayTag ActionTag) const;
	virtual bool CanServerApplyAction_Implementation(FGameplayTag ActionTag) const;

	// ---- Events ----

	/** Fired on ALL machines (via multicast) when the server confirms an action — for cosmetic FX. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Authority")
	FNet_OnActionConfirmed OnActionConfirmed;

	/** Fired locally when the replicated counter changes (server immediately; clients via OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Authority")
	FNet_OnActionCounterChanged OnActionCounterChanged;

protected:
	/**
	 * Server RPC: the client's request to perform an action. Reliable + WithValidation. The
	 * _Validate function performs cheap anti-cheat sanity checks (is the tag even valid?) and the
	 * _Implementation performs the authoritative apply. NEVER trust client-supplied state here
	 * beyond what validation/authority allows.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestAction(FGameplayTag ActionTag);

	/**
	 * Multicast RPC: cosmetic confirmation pushed to every connection (and the server). Carries NO
	 * authoritative state — that is what replicated properties are for — only the tag needed to
	 * play feedback. Unreliable would be acceptable for pure cosmetics; kept reliable here for the
	 * teaching example so the confirmation event is never dropped.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastActionConfirmed(FGameplayTag ActionTag);

private:
	/** Server-authoritative apply step, shared by the RPC and the local (already-authority) path. */
	void ApplyActionAuthoritative(FGameplayTag ActionTag);

	/** The last action the server confirmed. Replicated to all clients. */
	UPROPERTY(ReplicatedUsing = OnRep_LastConfirmedAction)
	FGameplayTag LastConfirmedAction;

	/**
	 * Bandwidth-light replicated count of confirmations for LastConfirmedAction. Demonstrates the
	 * quantized helper struct on the wire (0..255 in 8 bits). Replicated via OnRep.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ConfirmedCount)
	FNet_RepInt ConfirmedCount;

	/**
	 * Full per-tag counters, server-authoritative and NOT replicated (the small replicated surface
	 * above is what crosses the wire). Lives only on the authority; clients query the replicated
	 * LastConfirmedAction/ConfirmedCount instead.
	 */
	UPROPERTY()
	TMap<FGameplayTag, int32> ServerActionCounts;

	UFUNCTION()
	void OnRep_LastConfirmedAction();

	UFUNCTION()
	void OnRep_ConfirmedCount();
};
