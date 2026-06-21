// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Prog_ShopComponent.generated.h"

class UProg_ShopComponent;
class UProg_ShopDefinition;
struct FProg_ShopEntry;

/**
 * Why a server-side TryPurchase failed (or that it succeeded). Mirrored to the requesting client as
 * part of the purchase result so the buyer's UI can show a precise reason without re-deriving it.
 */
UENUM(BlueprintType)
enum class EProg_PurchaseResult : uint8
{
	/** Purchase completed: currency debited, item granted, stock decremented. */
	Success,
	/** The shop component had no valid UProg_ShopDefinition assigned. */
	NoShop,
	/** The entry index was out of range for the shop's catalogue. */
	BadEntry,
	/** The entry's unlock condition was not satisfied for this buyer. */
	Locked,
	/** The entry is finite-stock and sold out (remaining == 0). */
	OutOfStock,
	/** The buyer exposes no ISeam_Wallet, so affordability could not be checked. */
	NoWallet,
	/** The buyer cannot afford the entry's price in its currency. */
	CannotAfford,
	/** The buyer exposes no ISeam_PurchaseTarget, so the item could not be delivered. */
	NoPurchaseTarget,
	/** The buyer's purchase target rejected the item (full / filtered). */
	TargetRejected,
	/** The call was made off-authority (defensive; server re-checks everything). */
	NotAuthoritative
};

/**
 * One replicated stock counter: remaining purchases for the entry at EntryIndex.
 *
 * Only FINITE-stock entries get a stock entry; infinite-stock entries are simply absent from the
 * array (their remaining is conceptually unbounded). Wrapped as a fast-array item so a single
 * decrement delta-replicates rather than resending every counter.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopStockEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Index into the shop definition's Entries array this counter tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 EntryIndex = INDEX_NONE;

	/** Remaining purchases allowed for this entry. Always >= 0 while present. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 Remaining = 0;

	FProg_ShopStockEntry() = default;
	FProg_ShopStockEntry(int32 InIndex, int32 InRemaining) : EntryIndex(InIndex), Remaining(InRemaining) {}

	//~ FFastArraySerializerItem replication callbacks (called on clients only).
	void PostReplicatedAdd(const struct FProg_ShopStockArray& InArraySerializer);
	void PostReplicatedChange(const struct FProg_ShopStockArray& InArraySerializer);
	void PreReplicatedRemove(const struct FProg_ShopStockArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the per-entry remaining-stock counters.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed counters cross the wire. The
 * owning-component back-pointer is non-replicated and wired in the component ctor so the item
 * callbacks can surface a client-side change notification.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopStockArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated stock counters (finite-stock entries only). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	TArray<FProg_ShopStockEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UProg_ShopComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FProg_ShopStockEntry, FProg_ShopStockArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the stock array. */
template<>
struct TStructOpsTypeTraits<FProg_ShopStockArray> : public TStructOpsTypeTraitsBase2<FProg_ShopStockArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * A single offer projected for UI / affordability: the source entry's pricing plus the live remaining
 * stock resolved from the replicated counters. A plain value type (no pointers) so it is cheap to copy
 * to the viewmodel and safe to read on clients.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopOffer
{
	GENERATED_BODY()

	/** Index of the source entry in the shop definition. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 EntryIndex = INDEX_NONE;

	/** Item delivered on purchase. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag ItemTag;

	/** Quantity delivered per purchase. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 GrantCount = 1;

	/** Currency the price is denominated in. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag PriceCurrency;

	/** Unit price. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int64 Price = 0;

	/** Remaining purchases; -1 means infinite stock. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 Remaining = -1;

	/** True if a finite-stock offer has sold out. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bSoldOut = false;

	/** True if this offer has an unlock gate (UI may hide/grey it until satisfied). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bHasUnlockGate = false;
};

/** Broadcast (server and clients) whenever the shop's stock changes, so UI can refresh offers. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProg_OnShopStockChanged, UProg_ShopComponent*, Shop);

/**
 * Server-authoritative vendor component: owns a shop's live stock and executes purchases.
 *
 * Placed on a VENDOR actor (shopkeeper NPC, vending machine, store kiosk). References a
 * UProg_ShopDefinition (the immutable catalogue) and holds the only mutable, per-instance state: the
 * remaining stock per finite-stock entry, replicated as a fast-array so single decrements delta-
 * replicate. ALL state mutation is authority-guarded and early-returns on clients.
 *
 * Purchase flow (always runs on the server, invoked by a buyer's UProg_ShopClientComponent through a
 * validated server RPC):
 *   1. Re-validate entry index and the buyer.
 *   2. Evaluate the entry's unlock gate for this buyer (EvaluateEntryUnlock).
 *   3. Check finite stock.
 *   4. Read the buyer's ISeam_Wallet (read-only seam) for affordability.
 *   5. Verify the buyer's ISeam_PurchaseTarget can receive the item.
 *   6. Debit the buyer's currency by broadcasting an authoritative spend on the message bus (the
 *      buyer's concrete wallet component consumes it). Read-only ISeam_Wallet cannot be written, by
 *      contract; spending is an authority-only mutation that lives on the wallet, reached via the bus.
 *   7. Grant the item through ISeam_PurchaseTarget::GrantItem.
 *   8. Decrement finite stock and mark it dirty for replication.
 *
 * Cross-module coupling is ONLY through the Seams contracts and the message bus; the shop never hard-
 * includes a wallet/inventory implementation. Any unresolved seam degrades to a precise failure
 * EProg_PurchaseResult rather than a crash.
 */
UCLASS(ClassGroup = (DesignPatternsProgression), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSPROGRESSION_API UProg_ShopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UProg_ShopComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** The catalogue this vendor sells. Set in editor or at spawn; stock is (re)seeded from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop")
	TObjectPtr<UProg_ShopDefinition> ShopDefinition = nullptr;

	/**
	 * (Re)seed the replicated stock counters from ShopDefinition. AUTHORITY ONLY. Called automatically
	 * on BeginPlay; expose so a designer can restock at runtime (e.g. a daily reset). Clears existing
	 * counters and creates one per finite-stock entry at its authored initial Stock.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void SeedStockFromDefinition();

	/**
	 * Execute a purchase of the entry at EntryIndex for Buyer. AUTHORITY ONLY (early-returns
	 * NotAuthoritative off-server). Runs the full validated flow described on the class. Returns the
	 * outcome; on Success the buyer's currency is debited, the item granted, and finite stock
	 * decremented + replicated.
	 *
	 * @param Buyer       The actor receiving the item / paying. Must expose ISeam_Wallet and
	 *                    ISeam_PurchaseTarget (directly or via a component) for a paid item.
	 * @param EntryIndex  Index into ShopDefinition->Entries.
	 * @return The purchase outcome.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	EProg_PurchaseResult TryPurchase(AActor* Buyer, int32 EntryIndex);

	/** Remaining purchases for an entry; -1 if the entry is infinite-stock, 0 if sold out. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Shop")
	int32 GetRemainingStock(int32 EntryIndex) const;

	/** True if the entry is purchasable on stock grounds alone (infinite, or remaining > 0). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Shop")
	bool IsInStock(int32 EntryIndex) const;

	/**
	 * Project the shop's catalogue into a UI-ready offer list, folding in live remaining stock. Safe to
	 * call on clients (reads replicated counters + the definition's immutable data).
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void GetOffers(TArray<FProg_ShopOffer>& OutOffers) const;

	/**
	 * Evaluate whether the entry's unlock gate is satisfied for Buyer. Default implementation returns
	 * true when the entry has no UnlockCondition, and otherwise defers to the condition being non-null
	 * as "present but unevaluated" — projects override this BlueprintNativeEvent to drive the actual
	 * UProg_Condition::IsMet against their condition-source, keeping the shop decoupled from the
	 * condition module's evaluation signature. A null/invalid Buyer or entry index fails closed (false).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Progression|Shop")
	bool EvaluateEntryUnlock(const AActor* Buyer, int32 EntryIndex) const;
	virtual bool EvaluateEntryUnlock_Implementation(const AActor* Buyer, int32 EntryIndex) const;

	/** Fired (server and clients) after stock changes; UI binds this to refresh offers. */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Shop")
	FProg_OnShopStockChanged OnShopStockChanged;

	/** Called by the fast-array entry callbacks on clients to surface a stock change. */
	void HandleReplicatedStockChange();

private:
	/** Replicated per-entry remaining-stock counters (finite-stock entries only). */
	UPROPERTY(Replicated)
	FProg_ShopStockArray Stock;

	/** True once SeedStockFromDefinition has run (so BeginPlay does not double-seed after a restock). */
	bool bStockSeeded = false;

	/** Find the stock counter for EntryIndex, or null if absent (infinite stock or not yet seeded). */
	FProg_ShopStockEntry* FindStockEntry(int32 EntryIndex);
	const FProg_ShopStockEntry* FindStockEntry(int32 EntryIndex) const;

	/**
	 * Resolve a seam interface of type SeamClass off Buyer: first the actor itself (ImplementsInterface),
	 * then its components (FindComponentByInterface). Returns the UObject implementing it, or null.
	 */
	static UObject* ResolveSeamObject(const AActor* Buyer, TSubclassOf<UInterface> SeamClass);

	/**
	 * Resolve the buyer's concrete wallet component (a sibling area in THIS module), so the shop can
	 * issue an ATOMIC authoritative SpendCurrency rather than a fire-and-forget message. Same-module
	 * coupling is permitted (cross-MODULE coupling is what must go through seams). Returns null if the
	 * buyer has no wallet component. Resolved off the actor, never cached (lifetimes differ).
	 */
	static class UProg_WalletComponent* ResolveBuyerWallet(const AActor* Buyer);

	/** Server-side: notify listeners + (re)mark the dirty entry; broadcasts OnShopStockChanged. */
	void NotifyStockChanged();
};
