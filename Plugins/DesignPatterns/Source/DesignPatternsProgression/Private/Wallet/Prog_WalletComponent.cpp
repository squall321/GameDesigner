// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Wallet/Prog_WalletComponent.h"

#include "DesignPatternsProgressionModule.h"
#include "Settings/Prog_DeveloperSettings.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Wallet/Prog_WalletTypes.h"

//~ FProg_CurrencyEntry replication callbacks (client side) ------------------------------------

void FProg_CurrencyEntry::PreReplicatedRemove(const FProg_CurrencyArray& InArraySerializer)
{
	// A currency entry was removed on the server; reflect it locally as a zeroed balance.
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Currency, 0);
	}
}

void FProg_CurrencyEntry::PostReplicatedAdd(const FProg_CurrencyArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Currency, Amount);
	}
}

void FProg_CurrencyEntry::PostReplicatedChange(const FProg_CurrencyArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Currency, Amount);
	}
}

//~ UProg_WalletComponent ----------------------------------------------------------------------

UProg_WalletComponent::UProg_WalletComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Wallet.OwnerComponent = this;
}

void UProg_WalletComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// Re-assert the back-pointer in case the struct was default-constructed during load.
	Wallet.OwnerComponent = this;
}

void UProg_WalletComponent::BeginPlay()
{
	Super::BeginPlay();

	// This component IS the ISeam_Wallet provider for its owning actor. Consumers resolve it off the
	// owner via AActor::GetComponentByInterface(USeam_Wallet::StaticClass()), so there is nothing to
	// register in the global service locator (a wallet is per-actor, not a singleton). We log the
	// availability so the seam wiring is visible in verbose logs.
	if (AActor* Owner = GetOwner())
	{
		bRegisteredWalletSeam = true;
		UE_LOG(LogDP, Verbose, TEXT("[Prog_Wallet] ISeam_Wallet available on owner '%s'."), *Owner->GetName());
	}
}

void UProg_WalletComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bRegisteredWalletSeam = false;
	Super::EndPlay(EndPlayReason);
}

void UProg_WalletComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProg_WalletComponent, Wallet);
}

bool UProg_WalletComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

int64 UProg_WalletComponent::GetMaxBalance() const
{
	if (const UProg_DeveloperSettings* Settings = UProg_DeveloperSettings::Get())
	{
		return FMath::Max<int64>(0, Settings->MaxCurrencyBalance);
	}
	// Defensive fallback (CDO is never actually null): a generous but int64-safe cap.
	return 1000000000;
}

int32 UProg_WalletComponent::FindEntryIndex(const FGameplayTag& Currency) const
{
	for (int32 Index = 0; Index < Wallet.Entries.Num(); ++Index)
	{
		if (Wallet.Entries[Index].Currency == Currency)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FProg_CurrencyEntry& UProg_WalletComponent::FindOrAddEntry(const FGameplayTag& Currency)
{
	const int32 Existing = FindEntryIndex(Currency);
	if (Existing != INDEX_NONE)
	{
		return Wallet.Entries[Existing];
	}
	FProg_CurrencyEntry& NewEntry = Wallet.Entries.AddDefaulted_GetRef();
	NewEntry.Currency = Currency;
	return NewEntry;
}

int64 UProg_WalletComponent::AddCurrency(FGameplayTag Currency, int64 Amount)
{
	// AUTHORITY GUARD: never mutate replicated balances on a client.
	if (!HasAuthority())
	{
		return 0;
	}
	if (!Currency.IsValid() || Amount <= 0)
	{
		return 0;
	}

	FProg_CurrencyEntry& Entry = FindOrAddEntry(Currency);

	const int64 MaxBalance = GetMaxBalance();
	const int64 Before = Entry.Amount;

	// Clamp against the configured ceiling (0 means uncapped). Compute the headroom so we never
	// overflow int64 when summing.
	int64 NewBalance;
	if (MaxBalance > 0)
	{
		const int64 Headroom = FMath::Max<int64>(0, MaxBalance - Before);
		const int64 Applied = FMath::Min(Amount, Headroom);
		NewBalance = Before + Applied;
	}
	else
	{
		// Uncapped: still guard the rare int64 overflow.
		NewBalance = (Amount > TNumericLimits<int64>::Max() - Before) ? TNumericLimits<int64>::Max() : Before + Amount;
	}

	const int64 Added = NewBalance - Before;
	if (Added <= 0)
	{
		return 0; // Already at the ceiling.
	}

	Entry.Amount = NewBalance;
	Wallet.MarkItemDirty(Entry);
	NotifyBalanceChanged(Currency, NewBalance);

	UE_LOG(LogDP, Verbose, TEXT("[Prog_Wallet] +%lld %s -> %lld"), Added, *Currency.ToString(), NewBalance);
	return Added;
}

bool UProg_WalletComponent::SpendCurrency(FGameplayTag Currency, int64 Amount)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return false;
	}
	if (!Currency.IsValid() || Amount <= 0)
	{
		return false;
	}

	const int32 Index = FindEntryIndex(Currency);
	if (Index == INDEX_NONE)
	{
		return false; // No such currency: balance is 0, cannot afford.
	}

	FProg_CurrencyEntry& Entry = Wallet.Entries[Index];
	if (Entry.Amount < Amount)
	{
		return false; // Insufficient: spending never drives a balance negative.
	}

	Entry.Amount -= Amount;
	Wallet.MarkItemDirty(Entry);
	NotifyBalanceChanged(Currency, Entry.Amount);

	UE_LOG(LogDP, Verbose, TEXT("[Prog_Wallet] -%lld %s -> %lld"), Amount, *Currency.ToString(), Entry.Amount);
	return true;
}

int64 UProg_WalletComponent::SetBalance(FGameplayTag Currency, int64 NewBalance)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return 0;
	}
	if (!Currency.IsValid())
	{
		return 0;
	}

	const int64 MaxBalance = GetMaxBalance();
	int64 Clamped = FMath::Max<int64>(0, NewBalance);
	if (MaxBalance > 0)
	{
		Clamped = FMath::Min(Clamped, MaxBalance);
	}

	FProg_CurrencyEntry& Entry = FindOrAddEntry(Currency);
	if (Entry.Amount == Clamped)
	{
		return Clamped; // No change; avoid spurious replication/notification.
	}

	Entry.Amount = Clamped;
	Wallet.MarkItemDirty(Entry);
	NotifyBalanceChanged(Currency, Clamped);

	UE_LOG(LogDP, Verbose, TEXT("[Prog_Wallet] =%lld %s (set)"), Clamped, *Currency.ToString());
	return Clamped;
}

void UProg_WalletComponent::NotifyBalanceChanged(const FGameplayTag& Currency, int64 NewBalance)
{
	OnBalanceChanged.Broadcast(this, Currency, NewBalance);

	const UProg_DeveloperSettings* Settings = UProg_DeveloperSettings::Get();
	const bool bBroadcastBus = Settings ? Settings->bWalletBroadcastsOnBus : true;
	if (!bBroadcastBus)
	{
		return;
	}

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FProg_BalanceChangedEvent Payload;
		Payload.Currency = Currency;
		Payload.NewBalance = NewBalance;
		Payload.WalletOwner = GetOwner();
		Bus->BroadcastPayload(ProgTags::Bus_BalanceChanged, FInstancedStruct::Make(Payload), this);
	}
}

void UProg_WalletComponent::HandleReplicatedChange(const FGameplayTag& Currency, int64 NewBalance)
{
	// Reached on clients from the fast-array entry callbacks: surface the per-currency change. Clients
	// never broadcast on the bus from here (the server already did); only fire the local delegate.
	OnBalanceChanged.Broadcast(this, Currency, NewBalance);
}

//~ ISeam_Wallet (read-only) -------------------------------------------------------------------

int64 UProg_WalletComponent::GetBalance_Implementation(FGameplayTag CurrencyTag) const
{
	const int32 Index = FindEntryIndex(CurrencyTag);
	return Index != INDEX_NONE ? Wallet.Entries[Index].Amount : 0;
}

bool UProg_WalletComponent::CanAfford_Implementation(FGameplayTag CurrencyTag, int64 Amount) const
{
	if (Amount <= 0)
	{
		return true; // Nothing to afford.
	}
	return GetBalance_Implementation(CurrencyTag) >= Amount;
}

void UProg_WalletComponent::GetAllBalances_Implementation(TMap<FGameplayTag, int64>& OutBalances) const
{
	for (const FProg_CurrencyEntry& Entry : Wallet.Entries)
	{
		if (Entry.Currency.IsValid())
		{
			OutBalances.Add(Entry.Currency, Entry.Amount);
		}
	}
}

// ---- ISeam_WalletAuthority (write dual; delegates to the existing authority-guarded mutators) ----

bool UProg_WalletComponent::CanSpend_Implementation(FGameplayTag CurrencyTag, int64 Amount) const
{
	// Read-only: identical to CanAfford. Safe to call anywhere.
	return CanAfford_Implementation(CurrencyTag, Amount);
}

bool UProg_WalletComponent::Spend_Implementation(FGameplayTag CurrencyTag, int64 Amount)
{
	// AUTHORITY ONLY: SpendCurrency guards HasAuthority() at the top and returns false on clients.
	return SpendCurrency(CurrencyTag, Amount);
}

int64 UProg_WalletComponent::Grant_Implementation(FGameplayTag CurrencyTag, int64 Amount)
{
	// AUTHORITY ONLY: AddCurrency guards HasAuthority() at the top and is a no-op on clients.
	return AddCurrency(CurrencyTag, Amount);
}
