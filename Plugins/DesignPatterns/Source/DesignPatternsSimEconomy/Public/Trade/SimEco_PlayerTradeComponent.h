// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "SimEco_PlayerTradeComponent.generated.h"

class USimEco_PlayerTradeComponent;

/** Phase of a P2P trade session, replicated to the owning client (mirrored by both partners). */
UENUM(BlueprintType)
enum class ESimEco_TradePhase : uint8
{
	/** No active session. */
	Idle,
	/** A session is open; both sides edit offers; neither has confirmed. */
	Negotiating,
	/** Both sides have confirmed; the server is committing the swap (transient). */
	Committing
};

/** One staged item line in a trade offer (the offering side's item, escrowed on stage). Fast-array item. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_TradeStagedItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Item tag offered. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Trade")
	FGameplayTag ItemTag;

	/** Quantity offered (already escrowed out of the offerer's inventory). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Trade")
	int32 Quantity = 0;

	FSimEco_TradeStagedItem() = default;
	FSimEco_TradeStagedItem(const FGameplayTag& In, int32 Q) : ItemTag(In), Quantity(Q) {}

	void PostReplicatedAdd(const struct FSimEco_TradeStagedArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_TradeStagedArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_TradeStagedArray& InArraySerializer);
};

/** Fast-array of this side's staged items (TOP-LEVEL replicated member — never nested). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_TradeStagedArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Trade")
	TArray<FSimEco_TradeStagedItem> Items;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimEco_PlayerTradeComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_TradeStagedItem, FSimEco_TradeStagedArray>(Items, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FSimEco_TradeStagedArray> : public TStructOpsTypeTraitsBase2<FSimEco_TradeStagedArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server + owning client) on any trade-session change (offer edit / confirm / phase). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnTradeChanged, USimEco_PlayerTradeComponent*, Trade);

/**
 * Secure PLAYER-TO-PLAYER TRADE component (both-confirm escrow, anti-dupe).
 *
 * Each trading player carries one of these. A session is symmetric: each side stages an offer (items +
 * currency). On STAGE the goods physically leave the offerer (items via ISeam_TradableInventory::RemoveItem,
 * currency via ISeam_WalletAuthority::Spend) into THIS component's escrow — so a duped/cancelled trade
 * can always be rolled back to the exact escrowed amounts, and nothing is ever in two inventories at once.
 * ANY change to either side's offer clears BOTH confirmations (so you can't confirm then sneak-edit).
 * When BOTH sides are confirmed the server commits the swap atomically: each side receives the partner's
 * escrowed items (ISeam_PurchaseTarget::GrantItem) and currency (ISeam_WalletAuthority::Grant). Cancel /
 * disconnect refunds each side its own escrow.
 *
 * REPLICATION: the staged-items fast-array and scalar session state are TOP-LEVEL replicated members
 * (no nested fast-array). Client intent (open/stage/confirm/cancel) arrives via Server_* RPCs. EVERY
 * mutator guards authority at the TOP. The partner is resolved/validated server-side; a client never
 * names an arbitrary victim — both partners must be running this flow against each other.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_PlayerTradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_PlayerTradeComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// ---- Client entry points ----

	/** Open a trade session with the player owning PartnerTradeComp's actor. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestOpen(USimEco_PlayerTradeComponent* Partner);

	/** Stage Count of ItemTag into THIS side's offer (escrowed out of the owner's inventory). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestStageItem(FGameplayTag ItemTag, int32 Count);

	/** Set THIS side's offered currency to Amount of CurrencyTag (escrowed out of the owner's wallet). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestStageCurrency(FGameplayTag CurrencyTag, int64 Amount);

	/** Confirm THIS side's offer. When both sides are confirmed the server commits the swap. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestConfirm();

	/** Cancel the session; both sides are refunded their escrow. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestCancel();

	// ---- Read (owner-safe) ----

	/** Current session phase. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Trade")
	ESimEco_TradePhase GetPhase() const { return Phase; }

	/** This side's staged items (escrowed). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Trade")
	TArray<FSimEco_TradeStagedItem> GetStagedItems() const { return Staged.Items; }

	/** This side's staged currency tag / amount. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Trade")
	int64 GetStagedCurrency() const { return StagedCurrency; }

	/** True if THIS side has confirmed. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Trade")
	bool IsConfirmed() const { return bConfirmed; }

	/** Fired (server + owning client) on any session change. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Trade")
	FSimEco_OnTradeChanged OnTradeChanged;

	/** Called by the fast-array callbacks on the owning client. */
	void HandleReplicatedChange();

	/**
	 * SERVER: the partner component drives state changes through these (same module; concrete coupling
	 * permitted). Public so the partner can clear our confirmation / drive a commit. AUTHORITY ONLY.
	 */
	void ServerClearConfirmation();
	void ServerRefundEscrow();
	void ServerDeliverFrom(USimEco_PlayerTradeComponent* From); // grant From's escrow to THIS owner

protected:
	/** True if the owner has authority. */
	bool HasAuthority() const;

	/** Resolve the owner's inventory / wallet seam objects (may be null). */
	UObject* ResolveOwnerInventoryRemove() const; // ISeam_TradableInventory
	UObject* ResolveOwnerInventoryAdd() const;     // ISeam_PurchaseTarget
	UObject* ResolveOwnerWallet() const;           // ISeam_WalletAuthority

	/** Replicated state change handler. */
	UFUNCTION()
	void OnRep_Session();

	/** Server-side: notify + (re)broadcast. */
	void NotifyChanged();

	/** Server-side: try to commit if both sides are confirmed. */
	void TryCommit();

private:
	/** Replicated phase. */
	UPROPERTY(ReplicatedUsing = OnRep_Session)
	ESimEco_TradePhase Phase = ESimEco_TradePhase::Idle;

	/** Replicated: this side's confirmation flag. */
	UPROPERTY(ReplicatedUsing = OnRep_Session)
	bool bConfirmed = false;

	/** Replicated: this side's offered currency tag. */
	UPROPERTY(ReplicatedUsing = OnRep_Session)
	FGameplayTag StagedCurrencyTag;

	/** Replicated: this side's offered currency amount (escrowed). */
	UPROPERTY(ReplicatedUsing = OnRep_Session)
	int64 StagedCurrency = 0;

	/** Replicated TOP-LEVEL fast-array of this side's staged items. */
	UPROPERTY(Replicated)
	FSimEco_TradeStagedArray Staged;

	/** The trade partner's component (server-only; both sides point at each other). */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimEco_PlayerTradeComponent> Partner;

	/** Server-only: end the session, resetting all state. */
	void ServerEndSession(bool bRefund);

	/** Server-only: stage helpers (escrow out). */
	void ServerStageItem(const FGameplayTag& ItemTag, int32 Count);
	void ServerStageCurrency(const FGameplayTag& CurrencyTag, int64 Amount);
	void ServerOpen(USimEco_PlayerTradeComponent* InPartner);
	void ServerConfirm();

	UFUNCTION(Server, Reliable, WithValidation) void Server_Open(USimEco_PlayerTradeComponent* InPartner);
	UFUNCTION(Server, Reliable, WithValidation) void Server_StageItem(FGameplayTag ItemTag, int32 Count);
	UFUNCTION(Server, Reliable, WithValidation) void Server_StageCurrency(FGameplayTag CurrencyTag, int64 Amount);
	UFUNCTION(Server, Reliable, WithValidation) void Server_Confirm();
	UFUNCTION(Server, Reliable, WithValidation) void Server_Cancel();
};
