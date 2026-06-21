// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Economy/SimEco_StepListener.h"
#include "SimEco_BankComponent.generated.h"

class USimEco_BankComponent;
class USimEco_BankSettingsDef;
class USimEco_EconomySubsystem;

/** Why a bank operation failed (or succeeded). Mirrored to the owning client for UI. */
UENUM(BlueprintType)
enum class ESimEco_BankResult : uint8
{
	Success,
	/** No bank settings asset assigned. */
	NoSettings,
	/** The depositor exposes no wallet authority seam. */
	NoWallet,
	/** Insufficient wallet funds to deposit / repay. */
	InsufficientFunds,
	/** Insufficient bank balance to withdraw. */
	InsufficientBalance,
	/** A loan request exceeds the reputation-scaled maximum. */
	OverLoanLimit,
	/** Called off-authority. */
	NotAuthoritative,
	/** Malformed request. */
	BadRequest
};

/** One outstanding loan tranche: principal still owed, tracked as a fast-array item. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_LoanEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Stable id for this loan tranche (so repayment can target it). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Bank")
	int32 LoanId = INDEX_NONE;

	/** Remaining principal owed (interest is folded into principal each accrual). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Bank")
	int64 Principal = 0;

	FSimEco_LoanEntry() = default;

	//~ FFastArraySerializerItem replication callbacks (owning client only — COND_OwnerOnly array).
	void PostReplicatedAdd(const struct FSimEco_LoanArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_LoanArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_LoanArray& InArraySerializer);
};

/** Fast-array of the depositor's outstanding loans (owner-only replicated). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_LoanArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Bank")
	TArray<FSimEco_LoanEntry> Entries;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimEco_BankComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_LoanEntry, FSimEco_LoanArray>(Entries, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FSimEco_LoanArray> : public TStructOpsTypeTraitsBase2<FSimEco_LoanArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server + owning client) after a bank balance / loan change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnBankChanged, USimEco_BankComponent*, Bank);

/**
 * PLAYER-OWNED bank account: deposit / withdraw / loan, with per-step compounding interest.
 *
 * Holds a replicated deposit Balance (COND_OwnerOnly — a player's bank balance is private) and a
 * fast-array of outstanding Loans (also owner-only). Deposit moves currency OUT of the player's wallet
 * (ISeam_WalletAuthority::Spend) into Balance; withdraw moves it back (Grant). Interest accrues on
 * Balance and on loan principal each economy step (ISimEco_StepListener, registered on authority).
 *
 * Client intent arrives via Server_* RPCs (the component is player-owned so RPCs route). EVERY mutator
 * guards authority at the TOP. Loan limits are reputation-scaled via the bank settings asset, resolving
 * ISeam_Reputation off the owning player WITHOUT including the Narrative module.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_BankComponent
	: public UActorComponent
	, public ISimEco_StepListener
{
	GENERATED_BODY()

public:
	USimEco_BankComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISimEco_StepListener
	/** Server-only. Accrue deposit interest and loan interest per cadence. */
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) override;
	//~ End ISimEco_StepListener

	/** The bank-rules asset (rates, loan limits). Required for any operation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Bank")
	TObjectPtr<USimEco_BankSettingsDef> Settings = nullptr;

	// ---- Client entry points ----

	/** Request to deposit Amount of the bank currency from the owner's wallet. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Bank")
	void RequestDeposit(int64 Amount);

	/** Request to withdraw Amount of the bank currency back into the owner's wallet. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Bank")
	void RequestWithdraw(int64 Amount);

	/** Request a loan of Amount (subject to the reputation-scaled limit); proceeds go to the wallet. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Bank")
	void RequestLoan(int64 Amount);

	/** Request to repay Amount toward outstanding loans (oldest first); pays from the wallet. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Bank")
	void RequestRepay(int64 Amount);

	// ---- Read API (owner-safe) ----

	/** Current deposit balance. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Bank")
	int64 GetBalance() const { return Balance; }

	/** Total outstanding loan principal across all tranches. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Bank")
	int64 GetTotalOwed() const;

	/** The reputation-scaled maximum total loan this depositor may carry right now. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Bank")
	int64 GetMaxLoan() const;

	/** Fired (server + owning client) after a balance / loan change. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Bank")
	FSimEco_OnBankChanged OnBankChanged;

	/** Called by the loan fast-array callbacks on the owning client. */
	void HandleReplicatedChange();

	// ---- Authoritative executors (server) ----

	ESimEco_BankResult ExecuteDeposit(int64 Amount);
	ESimEco_BankResult ExecuteWithdraw(int64 Amount);
	ESimEco_BankResult ExecuteLoan(int64 Amount);
	ESimEco_BankResult ExecuteRepay(int64 Amount);

protected:
	/** True if the owner has authority. */
	bool HasAuthority() const;

	/** Resolve the owner's wallet-authority seam object (may be null). */
	UObject* ResolveWallet() const;

	/** Resolve the owner's reputation with the bank faction (fails closed to 0). */
	float ResolveReputation() const;

	/** Resolve the world economy driver. */
	USimEco_EconomySubsystem* ResolveEconomy() const;

	/** Replicated balance change handler. */
	UFUNCTION()
	void OnRep_Balance();

private:
	/** Replicated private deposit balance (owner-only). */
	UPROPERTY(ReplicatedUsing = OnRep_Balance)
	int64 Balance = 0;

	/** Replicated outstanding loans (owner-only). */
	UPROPERTY(Replicated)
	FSimEco_LoanArray Loans;

	/** Monotonic loan id source (server-only). */
	UPROPERTY(Replicated)
	int32 NextLoanId = 1;

	/** True once registered with the economy driver. */
	bool bRegisteredWithEconomy = false;

	/** Steps since last interest accrual. */
	int32 StepsSinceInterest = 0;

	/** Currency tag from settings (defensive empty if none). */
	FGameplayTag GetCurrencyTag() const;

	/** Server-side: broadcast change. */
	void NotifyChanged();

	/** Client RPC delivering an operation outcome to the owning client. */
	UFUNCTION(Client, Reliable)
	void Client_BankResult(ESimEco_BankResult Result, int64 NewBalance);

	UFUNCTION(Server, Reliable, WithValidation) void Server_Deposit(int64 Amount);
	UFUNCTION(Server, Reliable, WithValidation) void Server_Withdraw(int64 Amount);
	UFUNCTION(Server, Reliable, WithValidation) void Server_Loan(int64 Amount);
	UFUNCTION(Server, Reliable, WithValidation) void Server_Repay(int64 Amount);
};
