// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Prog_ShopClientComponent.generated.h"

// Used only by pointer below; the full definition is included in the .cpp.
class UProg_ShopComponent;

/**
 * Broadcast on the BUYER's owning client after a purchase request resolves on the server.
 *
 * @param Vendor      The vendor actor that owns the shop (may be null if it could not be resolved).
 * @param EntryIndex  The shop entry that was requested.
 * @param Result      The server-decided outcome.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProg_OnPurchaseResult,
	AActor*, Vendor, int32, EntryIndex, EProg_PurchaseResult, Result);

/**
 * Player-owned purchase-intent component: the client->server bridge for buying from a vendor shop.
 *
 * Lives on a PLAYER-OWNED actor (PlayerState / PlayerController / the player's pawn) so its server
 * RPC is routed and owned correctly. It carries NO authoritative or replicated gameplay state — the
 * vendor's UProg_ShopComponent is the sole authority on stock and the buyer's wallet/inventory are the
 * authorities on currency/items. This component only:
 *   - takes a local "I want to buy entry N from vendor V" intent (RequestPurchase),
 *   - forwards it as a validated, reliable server RPC (Server_Purchase),
 *   - and surfaces the server's decision back to local UI (OnPurchaseResult), driven by a client RPC.
 *
 * The server handler RE-DERIVES and RE-CHECKS everything the client claimed: it resolves the vendor's
 * shop component from the passed actor, enforces an interaction-range check between the buyer and the
 * vendor (so a client cannot buy from across the map), and then calls the shop's authoritative
 * TryPurchase — which itself re-validates unlock/stock/affordability. The client value is never
 * trusted; it is only a hint about WHAT to attempt.
 */
UCLASS(ClassGroup = (DesignPatternsProgression), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSPROGRESSION_API UProg_ShopClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProg_ShopClientComponent();

	/**
	 * Maximum distance (cm) between the buyer's pawn and the vendor actor for a purchase to be allowed,
	 * re-checked SERVER-SIDE. A tunable (not a hardcoded gameplay constant): set per project / per
	 * buyer archetype. 0 or negative disables the range check (e.g. for menu-style remote shops).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Shop", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxInteractionDistance = 350.f;

	/**
	 * Local entry point: request to buy entry EntryIndex from Vendor's shop. Callable on the owning
	 * client (or on a listen-server host, where it routes straight through). Resolves the buyer actor
	 * from this component's owner and issues the validated server RPC. Does NOT itself mutate any state.
	 *
	 * @param Vendor      The vendor actor hosting a UProg_ShopComponent.
	 * @param EntryIndex  Index into that shop's catalogue.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void RequestPurchase(AActor* Vendor, int32 EntryIndex);

	/** Fired on the buyer's client (and on the host) with the server's purchase decision. */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Shop")
	FProg_OnPurchaseResult OnPurchaseResult;

private:
	/**
	 * Validated server RPC carrying the purchase intent. Validation rejects a null vendor or a negative
	 * entry index outright; the body re-derives the buyer, re-checks interaction range, resolves the
	 * vendor's shop component and calls its authoritative TryPurchase, then reports the result back to
	 * the owning client.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Purchase(AActor* Vendor, int32 EntryIndex);

	/** Client RPC delivering the server's decision back to the requesting client's UI. */
	UFUNCTION(Client, Reliable)
	void Client_PurchaseResult(AActor* Vendor, int32 EntryIndex, EProg_PurchaseResult Result);

	/** Resolve the actor that actually buys (the player pawn/character) from this component's owner. */
	AActor* ResolveBuyerActor() const;

	/**
	 * Server-side interaction-range gate between Buyer and Vendor. Returns true when within
	 * MaxInteractionDistance, or when the range check is disabled (MaxInteractionDistance <= 0) or the
	 * actors lack positions to compare. Logged on rejection.
	 */
	bool IsWithinInteractionRange(const AActor* Buyer, const AActor* Vendor) const;

	/** Resolve the vendor's shop component from a vendor actor (its first UProg_ShopComponent). */
	static UProg_ShopComponent* ResolveShop(const AActor* Vendor);
};
