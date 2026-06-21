// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Economy/Seam_Wallet.h"
#include "Economy/Seam_WalletAuthority.h"
#include "Prog_WalletComponent.generated.h"

class UProg_WalletComponent;

/**
 * One currency's holding in a wallet.
 *
 * Tracked as a fast-array item so an individual currency change delta-replicates instead of resending
 * the whole purse. Amount is an int64 (currencies are whole units; fractional currency is a design
 * smell). Authority enforces Amount >= 0 at every mutation, so a replicated item is always valid.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_CurrencyEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Identity tag of the currency (a child of DP.Prog.Currency authored by the project). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	FGameplayTag Currency;

	/** Current balance of this currency. Authority guarantees this is always >= 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	int64 Amount = 0;

	FProg_CurrencyEntry() = default;
	explicit FProg_CurrencyEntry(const FGameplayTag& InCurrency) : Currency(InCurrency) {}

	//~ FFastArraySerializerItem replication callbacks (client side). Each forwards to the owner so the
	//   component can surface a per-currency change and fire its delegate/bus event after replication.
	void PreReplicatedRemove(const struct FProg_CurrencyArray& InArraySerializer);
	void PostReplicatedAdd(const struct FProg_CurrencyArray& InArraySerializer);
	void PostReplicatedChange(const struct FProg_CurrencyArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the wallet's per-currency entries. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed currencies cross the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_CurrencyArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated currency entries. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	TArray<FProg_CurrencyEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UProg_WalletComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FProg_CurrencyEntry, FProg_CurrencyArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the currency array. */
template<>
struct TStructOpsTypeTraits<FProg_CurrencyArray> : public TStructOpsTypeTraitsBase2<FProg_CurrencyArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Broadcast (server and clients) whenever a currency balance changes.
 * @param Wallet     The wallet component whose balance changed.
 * @param Currency   The currency tag that changed.
 * @param NewBalance The post-change balance of that currency.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FProg_OnBalanceChanged,
	UProg_WalletComponent*, Wallet, FGameplayTag, Currency, int64, NewBalance);

/**
 * Server-authoritative, replicated, tag-keyed currency purse. IMPLEMENTS the read-only ISeam_Wallet
 * seam so shops, quests, the HUD and skill cost-checks can read balances without depending on this
 * module: resolve the wallet off an actor with AActor::GetComponentByInterface(USeam_Wallet) or
 * Implements<USeam_Wallet>() and call GetBalance/CanAfford/GetAllBalances.
 *
 * REPLICATION: balances delta-replicate via a FFastArraySerializer (FProg_CurrencyArray). The two
 * authoritative mutators (AddCurrency / SpendCurrency) guard HasAuthority() at the TOP and early-return
 * on clients, so clients only ever observe balances through replication + OnBalanceChanged. There is no
 * client->server "spend" RPC here: spending intent is the caller's responsibility (a shop's
 * player-owned component issues a validated server request and then calls SpendCurrency on authority).
 *
 * The component is meant to live on a player-state / controller / pawn (anything that replicates to its
 * owning client). It does not tick.
 */
UCLASS(ClassGroup = (DesignPatternsProgression), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSPROGRESSION_API UProg_WalletComponent : public UActorComponent, public ISeam_Wallet, public ISeam_WalletAuthority
{
	GENERATED_BODY()

public:
	UProg_WalletComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Authoritative mutators (no-op on clients) ----

	/**
	 * Add Amount units of Currency to the purse. AUTHORITY ONLY (no-op + returns 0 on clients).
	 * Validates Currency is valid and Amount > 0; clamps the resulting balance to the configured
	 * MaxCurrencyBalance. Returns the amount actually added (0 if rejected).
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Wallet")
	int64 AddCurrency(FGameplayTag Currency, int64 Amount);

	/**
	 * Spend Amount units of Currency. AUTHORITY ONLY (no-op + returns false on clients).
	 * Fails (returns false, no mutation) if Currency is invalid, Amount <= 0, or the balance is
	 * insufficient — spending never drives a balance negative. Returns true on a successful full spend.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Wallet")
	bool SpendCurrency(FGameplayTag Currency, int64 Amount);

	/**
	 * Force a currency to an exact balance. AUTHORITY ONLY. Validates non-negative and clamps to the
	 * configured maximum. Intended for save-restore / debug, NOT routine gameplay (use Add/Spend).
	 * Returns the balance actually set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Wallet")
	int64 SetBalance(FGameplayTag Currency, int64 NewBalance);

	// ---- ISeam_Wallet (read-only; client-safe) ----

	/** Current balance of CurrencyTag (0 if the wallet has no such currency). */
	virtual int64 GetBalance_Implementation(FGameplayTag CurrencyTag) const override;

	/** True if the wallet holds at least Amount of CurrencyTag. */
	virtual bool CanAfford_Implementation(FGameplayTag CurrencyTag, int64 Amount) const override;

	/** Append every (currency tag -> balance) pair this wallet holds. */
	virtual void GetAllBalances_Implementation(TMap<FGameplayTag, int64>& OutBalances) const override;

	//~ Begin ISeam_WalletAuthority — the authority WRITE dual of the read-only ISeam_Wallet, so economy
	// systems (shop / bank / merchant / reward) debit and credit the wallet without depending on this module.
	/** True if a Spend of Amount would currently succeed (read-only, safe anywhere). */
	virtual bool CanSpend_Implementation(FGameplayTag CurrencyTag, int64 Amount) const override;
	/** Debit Amount of CurrencyTag. AUTHORITY ONLY (delegates to SpendCurrency, which guards). */
	virtual bool Spend_Implementation(FGameplayTag CurrencyTag, int64 Amount) override;
	/** Credit Amount of CurrencyTag. AUTHORITY ONLY (delegates to AddCurrency, which guards). */
	virtual int64 Grant_Implementation(FGameplayTag CurrencyTag, int64 Amount) override;
	//~ End ISeam_WalletAuthority

	// ---- Read helpers (client-safe) ----

	/** Snapshot copy of all entries (safe on clients). */
	UFUNCTION(BlueprintCallable, Category = "Progression|Wallet")
	TArray<FProg_CurrencyEntry> GetAllEntries() const { return Wallet.Entries; }

	/** Broadcast whenever a balance changes (after replication on clients). */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Wallet")
	FProg_OnBalanceChanged OnBalanceChanged;

	/**
	 * Called by the fast-array entry callbacks on clients to surface a per-currency change. Public so
	 * the item structs (defined in this header) can forward to it.
	 */
	void HandleReplicatedChange(const FGameplayTag& Currency, int64 NewBalance);

protected:
	//~ Begin UActorComponent
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** True if this component's owner has network authority. */
	bool HasAuthority() const;

	/** Find an entry index by currency tag, or INDEX_NONE. */
	int32 FindEntryIndex(const FGameplayTag& Currency) const;

	/** Find or create the entry for Currency (authority side). Returns a stable reference. */
	FProg_CurrencyEntry& FindOrAddEntry(const FGameplayTag& Currency);

	/**
	 * Common post-mutation path (authority): mark the item dirty, fire the delegate and (if enabled in
	 * settings) broadcast the DP.Bus.Prog.BalanceChanged message.
	 */
	void NotifyBalanceChanged(const FGameplayTag& Currency, int64 NewBalance);

	/** Configured maximum per-currency balance (0 = uncapped); CDO-backed, never null. */
	int64 GetMaxBalance() const;

private:
	/** Replicated per-currency balances. */
	UPROPERTY(Replicated)
	FProg_CurrencyArray Wallet;

	/**
	 * True once this component has registered itself as the ISeam_Wallet provider for its owner. Used to
	 * keep registration idempotent and to unregister exactly once on EndPlay.
	 */
	bool bRegisteredWalletSeam = false;
};
