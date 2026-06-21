// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_Persistable.h"
#include "Economy/SimEco_StepListener.h"
#include "Auction/SimEco_AuctionTypes.h"
#include "GameplayTagContainer.h"

// FInstancedStruct band gate (StructUtils on 5.3/5.4, CoreUObject on 5.5).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimEco_AuctionSubsystem.generated.h"

class ASimEco_AuctionReplicationProxy;
class USimEco_EconomySubsystem;

/**
 * Durable per-lot save record: the public summary fields plus the escrowed item (so a save can restore
 * a listed lot's goods). Escrowed CURRENCY is intentionally NOT persisted across a fresh load — on
 * restore, lots are reconstituted as item-escrowed with their last summary; outstanding currency bids
 * are dropped to neutral and the lot reverts to no-bid (a conservative, dupe-safe restore policy).
 */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_AuctionSavedLot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) int32 LotId = INDEX_NONE;
	UPROPERTY(SaveGame) FGameplayTag ItemTag;
	UPROPERTY(SaveGame) int32 Quantity = 1;
	UPROPERTY(SaveGame) FGameplayTag CurrencyTag;
	UPROPERTY(SaveGame) int64 MinBid = 0;
	UPROPERTY(SaveGame) int64 BuyoutPrice = 0;
	UPROPERTY(SaveGame) int32 SettlementDay = 0;
};

/** Durable record of the whole auction house (active lots only). */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_AuctionSaveRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) TArray<FSimEco_AuctionSavedLot> SavedLots;
	UPROPERTY(SaveGame) int32 NextLotId = 1;
};

/** Outcome of an auction operation (list/bid/buyout), returned to the caller and mirrored to clients. */
UENUM(BlueprintType)
enum class ESimEco_AuctionResult : uint8
{
	Success,
	/** The lot id was not found / not active. */
	NoSuchLot,
	/** Seller could not escrow the item (not held / soulbound). */
	CannotEscrowItem,
	/** Bidder could not afford / escrow the bid currency. */
	CannotEscrowCurrency,
	/** Bid below the minimum / not exceeding the current high bid. */
	BidTooLow,
	/** No buyout configured on the lot. */
	NoBuyout,
	/** Called off-authority. */
	NotAuthoritative,
	/** Malformed request. */
	BadRequest
};

/**
 * Server-only escrow record for one lot: the seller's staged goods and the high bidder's staged
 * currency. NEVER replicated (anti-dupe — the goods physically left the seller's inventory and live
 * here until settlement). Bidder/seller actors are tracked weakly.
 */
USTRUCT()
struct FSimEco_AuctionEscrow
{
	GENERATED_BODY()

	/** The seller actor (weak; the lot survives the seller logging out only if you persist by id). */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> Seller;

	/** The current high bidder actor (weak). */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> HighBidder;

	/** Item escrowed out of the seller (returned on expiry, delivered to winner on settle). */
	UPROPERTY()
	FGameplayTag ItemTag;

	UPROPERTY()
	int32 Quantity = 1;

	/** Currency escrowed out of the high bidder (refunded if outbid; paid to seller on settle). */
	UPROPERTY()
	FGameplayTag CurrencyTag;

	/** Amount of currency currently escrowed from the high bidder. */
	UPROPERTY()
	int64 EscrowedCurrency = 0;
};

/**
 * World-scoped, server-authoritative AUCTION HOUSE: list / bid / buyout, escrow, day-rollover settle.
 *
 * The public lot summaries replicate via ASimEco_AuctionReplicationProxy (server-spawned AInfo). The
 * SENSITIVE escrow ledger (who staged which items / currency) stays here, server-only, never on the
 * wire — this is the anti-dupe guarantee: a listed item physically leaves the seller's inventory
 * (ISeam_TradableInventory::RemoveItem) into escrow, and a bid's currency leaves the bidder's wallet
 * (ISeam_WalletAuthority::Spend) into escrow; an outbid bidder is refunded.
 *
 * The public API (ListItem/PlaceBid/Buyout) is AUTHORITY-DRIVEN (not RPCs): client intent arrives via
 * USimEco_AuctionClientComponent's Server_* RPCs, which call these under authority. Settlement runs on
 * sim-day rollover via ISimEco_StepListener (resolved day number). Persistable so live lots + escrow
 * survive a save.
 */
UCLASS()
class DESIGNPATTERNSSIMECONOMY_API USimEco_AuctionSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_Persistable
	, public ISimEco_StepListener
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** UWorldSubsystem has no HasWorldAuthority of its own — declare our own. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISimEco_StepListener
	/** Server-only. On a new sim day, settle/expire any lots whose SettlementDay has passed. */
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) override;
	//~ End ISimEco_StepListener

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	// ---- Authority-driven API (NOT RPCs; called by the client component on the server) ----

	/**
	 * List Quantity of ItemTag for auction. AUTHORITY ONLY. Escrows the goods OUT of Seller's inventory
	 * via ISeam_TradableInventory before the lot becomes active (anti-dupe). Returns the new LotId via
	 * OutLotId. DurationDays sets the settlement day relative to the current sim day.
	 */
	ESimEco_AuctionResult ListItem(AActor* Seller, const FGameplayTag& ItemTag, int32 Quantity,
		const FGameplayTag& CurrencyTag, int64 MinBid, int64 BuyoutPrice, int32 DurationDays, int32& OutLotId);

	/**
	 * Place a bid of Amount on LotId. AUTHORITY ONLY. Escrows the bid currency OUT of Bidder's wallet;
	 * refunds the previous high bidder. The bid must exceed both MinBid and the current high bid.
	 */
	ESimEco_AuctionResult PlaceBid(AActor* Bidder, int32 LotId, int64 Amount);

	/**
	 * Buy LotId outright at its buyout price. AUTHORITY ONLY. Escrows the buyout currency, settles the
	 * lot immediately (goods to buyer, currency to seller), refunds any prior high bidder.
	 */
	ESimEco_AuctionResult Buyout(AActor* Buyer, int32 LotId);

	/** AUTHORITY ONLY: cancel a lot that has NO bids; returns escrowed goods to the seller. */
	ESimEco_AuctionResult CancelLot(AActor* Seller, int32 LotId);

	// ---- Read ----

	/** The replicated auction board proxy (server: the spawned one; resolvable by clients off world). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Auction")
	ASimEco_AuctionReplicationProxy* GetProxy() const { return Proxy.Get(); }

private:
	/** Server-only escrow ledger keyed by lot id. NEVER replicated. */
	UPROPERTY(Transient)
	TMap<int32, FSimEco_AuctionEscrow> Escrow;

	/** Public lot summaries mirror (server-side source of truth, pushed into the proxy). */
	UPROPERTY(Transient)
	TArray<FSimEco_AuctionLot> Lots;

	/** Replicated board carrier (server-spawned). Held weakly (a GI/world object referencing an actor). */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASimEco_AuctionReplicationProxy> Proxy;

	/** Monotonic lot id source. */
	int32 NextLotId = 1;

	/** Last sim day we ran settlement for (so settlement runs once per day). */
	int32 LastSettledDay = -1;

	/** True once registered with the economy driver. */
	bool bRegisteredWithEconomy = false;

	/** Spawn the replication proxy on authority (idempotent). */
	void EnsureProxy();

	/** Resolve the world economy driver (for step registration + day number). */
	USimEco_EconomySubsystem* ResolveEconomy() const;

	/** Find a lot summary by id (mutable / const). */
	FSimEco_AuctionLot* FindLot(int32 LotId);
	const FSimEco_AuctionLot* FindLot(int32 LotId) const;

	/** Push a lot summary into the proxy + local mirror. */
	void SyncLotToProxy(const FSimEco_AuctionLot& Lot);

	/** Settle one lot at its settlement: deliver goods to winner (or back to seller), pay seller. */
	void SettleLot(int32 LotId, bool bExpired);

	/** Resolve the current sim day (from the economy driver's clock; 0 if unavailable). */
	int32 ResolveCurrentDay() const;

	/** Resolve a stable display id for an actor (ISeam_EntityIdentity if present, else a fresh guid). */
	static FSeam_EntityId ResolveEntityId(const AActor* Actor);

	/** Refund the currently-escrowed currency back to the high bidder of a lot (if any). */
	void RefundHighBidder(int32 LotId);

	/** Broadcast an auction-changed notification on the bus (after-the-fact). */
	void NotifyAuctionChanged(AActor* Instigator) const;
};
