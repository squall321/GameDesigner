// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Bank/SimEco_BankComponent.h"
#include "Bank/SimEco_BankSettingsDef.h"
#include "Pricing/SimEco_EconomyReputation.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/SimEco_EconomySubsystem.h"
#include "Economy/Seam_WalletAuthority.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// ---- Loan fast-array item callbacks ----
void FSimEco_LoanEntry::PostReplicatedAdd(const FSimEco_LoanArray& In)    { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }
void FSimEco_LoanEntry::PostReplicatedChange(const FSimEco_LoanArray& In) { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }
void FSimEco_LoanEntry::PreReplicatedRemove(const FSimEco_LoanArray& In)  { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }

USimEco_BankComponent::USimEco_BankComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Loans.OwnerComponent = this;
}

void USimEco_BankComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// A bank account is private: only the owning client sees its balance and loans.
	DOREPLIFETIME_CONDITION(USimEco_BankComponent, Balance, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_BankComponent, Loans, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_BankComponent, NextLoanId, COND_OwnerOnly);
}

void USimEco_BankComponent::BeginPlay()
{
	Super::BeginPlay();
	Loans.OwnerComponent = this;

	if (HasAuthority())
	{
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->RegisterStepListener(Self);
			bRegisteredWithEconomy = true;
		}
	}
}

void USimEco_BankComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegisteredWithEconomy)
	{
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->UnregisterStepListener(Self);
		}
		bRegisteredWithEconomy = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool USimEco_BankComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_EconomySubsystem* USimEco_BankComponent::ResolveEconomy() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this);
}

FGameplayTag USimEco_BankComponent::GetCurrencyTag() const
{
	return Settings ? Settings->CurrencyTag : FGameplayTag();
}

UObject* USimEco_BankComponent::ResolveWallet() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (Owner->GetClass()->ImplementsInterface(USeam_WalletAuthority::StaticClass()))
	{
		return const_cast<AActor*>(Owner);
	}
	return const_cast<AActor*>(Owner)->FindComponentByInterface(USeam_WalletAuthority::StaticClass());
}

float USimEco_BankComponent::ResolveReputation() const
{
	float Rep = 0.0f;
	if (Settings && Settings->BankFactionTag.IsValid())
	{
		FSimEco_EconomyReputation::TryGetReputation(this, GetOwner(), Settings->BankFactionTag, Rep);
	}
	return Rep;
}

int64 USimEco_BankComponent::GetTotalOwed() const
{
	int64 Sum = 0;
	for (const FSimEco_LoanEntry& L : Loans.Entries)
	{
		Sum += L.Principal;
	}
	return Sum;
}

int64 USimEco_BankComponent::GetMaxLoan() const
{
	return Settings ? Settings->ComputeMaxLoan(ResolveReputation()) : 0;
}

void USimEco_BankComponent::OnRep_Balance()
{
	OnBankChanged.Broadcast(this);
}

void USimEco_BankComponent::HandleReplicatedChange()
{
	OnBankChanged.Broadcast(this);
}

void USimEco_BankComponent::NotifyChanged()
{
	OnBankChanged.Broadcast(this);
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_BankChanged, FInstancedStruct(), GetOwner());
	}
}

// ---- Step listener: interest ----

void USimEco_BankComponent::AdvanceEconomyStep(double /*StepSeconds*/, int64 /*StepIndex*/, int32 /*DayNumber*/)
{
	if (!HasAuthority() || !Settings)
	{
		return;
	}

	const int32 Cadence = FMath::Max(0, Settings->InterestEveryNSteps);
	if (!(Cadence == 0 || ++StepsSinceInterest >= Cadence))
	{
		return;
	}
	StepsSinceInterest = 0;

	bool bChanged = false;

	// Deposit interest (compounding).
	if (Balance >= Settings->MinInterestBalance && Settings->InterestPerStep > 0.0)
	{
		const int64 Earned = (int64)FMath::FloorToDouble((double)Balance * Settings->InterestPerStep);
		if (Earned > 0)
		{
			Balance += Earned;
			bChanged = true;
		}
	}

	// Loan interest (folds into principal).
	if (Settings->LoanInterestPerStep > 0.0)
	{
		for (FSimEco_LoanEntry& L : Loans.Entries)
		{
			const int64 Charge = (int64)FMath::CeilToDouble((double)L.Principal * Settings->LoanInterestPerStep);
			if (Charge > 0)
			{
				L.Principal += Charge;
				Loans.MarkItemDirty(L);
				bChanged = true;
			}
		}
	}

	if (bChanged)
	{
		NotifyChanged();
	}
}

// ---- Client entry points ----

void USimEco_BankComponent::RequestDeposit(int64 Amount)
{
	if (HasAuthority()) { const ESimEco_BankResult R = ExecuteDeposit(Amount); OnBankChanged.Broadcast(this); Client_BankResult(R, Balance); }
	else { Server_Deposit(Amount); }
}
void USimEco_BankComponent::RequestWithdraw(int64 Amount)
{
	if (HasAuthority()) { const ESimEco_BankResult R = ExecuteWithdraw(Amount); OnBankChanged.Broadcast(this); Client_BankResult(R, Balance); }
	else { Server_Withdraw(Amount); }
}
void USimEco_BankComponent::RequestLoan(int64 Amount)
{
	if (HasAuthority()) { const ESimEco_BankResult R = ExecuteLoan(Amount); OnBankChanged.Broadcast(this); Client_BankResult(R, Balance); }
	else { Server_Loan(Amount); }
}
void USimEco_BankComponent::RequestRepay(int64 Amount)
{
	if (HasAuthority()) { const ESimEco_BankResult R = ExecuteRepay(Amount); OnBankChanged.Broadcast(this); Client_BankResult(R, Balance); }
	else { Server_Repay(Amount); }
}

// ---- Server RPCs ----

bool USimEco_BankComponent::Server_Deposit_Validate(int64 Amount)  { return Amount > 0; }
bool USimEco_BankComponent::Server_Withdraw_Validate(int64 Amount) { return Amount > 0; }
bool USimEco_BankComponent::Server_Loan_Validate(int64 Amount)     { return Amount > 0; }
bool USimEco_BankComponent::Server_Repay_Validate(int64 Amount)    { return Amount > 0; }

void USimEco_BankComponent::Server_Deposit_Implementation(int64 Amount)  { const ESimEco_BankResult R = ExecuteDeposit(Amount);  Client_BankResult(R, Balance); }
void USimEco_BankComponent::Server_Withdraw_Implementation(int64 Amount) { const ESimEco_BankResult R = ExecuteWithdraw(Amount); Client_BankResult(R, Balance); }
void USimEco_BankComponent::Server_Loan_Implementation(int64 Amount)     { const ESimEco_BankResult R = ExecuteLoan(Amount);     Client_BankResult(R, Balance); }
void USimEco_BankComponent::Server_Repay_Implementation(int64 Amount)    { const ESimEco_BankResult R = ExecuteRepay(Amount);    Client_BankResult(R, Balance); }

void USimEco_BankComponent::Client_BankResult_Implementation(ESimEco_BankResult /*Result*/, int64 /*NewBalance*/)
{
	OnBankChanged.Broadcast(this);
}

// ---- Authoritative executors ----

ESimEco_BankResult USimEco_BankComponent::ExecuteDeposit(int64 Amount)
{
	if (!HasAuthority()) return ESimEco_BankResult::NotAuthoritative;
	if (Amount <= 0)     return ESimEco_BankResult::BadRequest;
	if (!Settings)       return ESimEco_BankResult::NoSettings;

	const FGameplayTag Currency = GetCurrencyTag();
	UObject* Wallet = ResolveWallet();
	if (!Wallet || !Currency.IsValid()) return ESimEco_BankResult::NoWallet;

	if (!ISeam_WalletAuthority::Execute_CanSpend(Wallet, Currency, Amount)) return ESimEco_BankResult::InsufficientFunds;
	if (!ISeam_WalletAuthority::Execute_Spend(Wallet, Currency, Amount))    return ESimEco_BankResult::InsufficientFunds;

	Balance += Amount;
	NotifyChanged();
	return ESimEco_BankResult::Success;
}

ESimEco_BankResult USimEco_BankComponent::ExecuteWithdraw(int64 Amount)
{
	if (!HasAuthority()) return ESimEco_BankResult::NotAuthoritative;
	if (Amount <= 0)     return ESimEco_BankResult::BadRequest;
	if (!Settings)       return ESimEco_BankResult::NoSettings;
	if (Balance < Amount) return ESimEco_BankResult::InsufficientBalance;

	const FGameplayTag Currency = GetCurrencyTag();
	UObject* Wallet = ResolveWallet();
	if (!Wallet || !Currency.IsValid()) return ESimEco_BankResult::NoWallet;

	// Move money first out of the bank, then credit the wallet; on a grant failure put it back.
	Balance -= Amount;
	const int64 Granted = ISeam_WalletAuthority::Execute_Grant(Wallet, Currency, Amount);
	if (Granted < Amount)
	{
		Balance += (Amount - Granted); // restore the unbanked remainder
	}
	NotifyChanged();
	return ESimEco_BankResult::Success;
}

ESimEco_BankResult USimEco_BankComponent::ExecuteLoan(int64 Amount)
{
	if (!HasAuthority()) return ESimEco_BankResult::NotAuthoritative;
	if (Amount <= 0)     return ESimEco_BankResult::BadRequest;
	if (!Settings)       return ESimEco_BankResult::NoSettings;

	const int64 MaxLoan = GetMaxLoan();
	if (GetTotalOwed() + Amount > MaxLoan) return ESimEco_BankResult::OverLoanLimit;

	const FGameplayTag Currency = GetCurrencyTag();
	UObject* Wallet = ResolveWallet();
	if (!Wallet || !Currency.IsValid()) return ESimEco_BankResult::NoWallet;

	// Credit the wallet with the loan proceeds, then book the principal.
	const int64 Granted = ISeam_WalletAuthority::Execute_Grant(Wallet, Currency, Amount);
	if (Granted <= 0) return ESimEco_BankResult::NoWallet;

	FSimEco_LoanEntry Loan;
	Loan.LoanId = NextLoanId++;
	Loan.Principal = Granted;
	Loans.Entries.Add(Loan);
	Loans.MarkArrayDirty();
	NotifyChanged();
	return ESimEco_BankResult::Success;
}

ESimEco_BankResult USimEco_BankComponent::ExecuteRepay(int64 Amount)
{
	if (!HasAuthority()) return ESimEco_BankResult::NotAuthoritative;
	if (Amount <= 0)     return ESimEco_BankResult::BadRequest;
	if (!Settings)       return ESimEco_BankResult::NoSettings;

	const FGameplayTag Currency = GetCurrencyTag();
	UObject* Wallet = ResolveWallet();
	if (!Wallet || !Currency.IsValid()) return ESimEco_BankResult::NoWallet;

	const int64 Owed = GetTotalOwed();
	const int64 Pay = FMath::Min(Amount, Owed);
	if (Pay <= 0) return ESimEco_BankResult::Success; // nothing owed

	if (!ISeam_WalletAuthority::Execute_CanSpend(Wallet, Currency, Pay)) return ESimEco_BankResult::InsufficientFunds;
	if (!ISeam_WalletAuthority::Execute_Spend(Wallet, Currency, Pay))    return ESimEco_BankResult::InsufficientFunds;

	// Apply payment oldest-loan-first.
	int64 Remaining = Pay;
	for (int32 i = 0; i < Loans.Entries.Num() && Remaining > 0; )
	{
		FSimEco_LoanEntry& L = Loans.Entries[i];
		const int64 Applied = FMath::Min(Remaining, L.Principal);
		L.Principal -= Applied;
		Remaining -= Applied;
		if (L.Principal <= 0)
		{
			Loans.Entries.RemoveAt(i);
			Loans.MarkArrayDirty();
		}
		else
		{
			Loans.MarkItemDirty(L);
			++i;
		}
	}
	NotifyChanged();
	return ESimEco_BankResult::Success;
}
