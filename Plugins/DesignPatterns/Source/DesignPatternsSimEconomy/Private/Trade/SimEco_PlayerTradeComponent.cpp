// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trade/SimEco_PlayerTradeComponent.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/Seam_TradableInventory.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "Economy/Seam_WalletAuthority.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// ---- Fast-array item callbacks ----
void FSimEco_TradeStagedItem::PostReplicatedAdd(const FSimEco_TradeStagedArray& In)    { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }
void FSimEco_TradeStagedItem::PostReplicatedChange(const FSimEco_TradeStagedArray& In) { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }
void FSimEco_TradeStagedItem::PreReplicatedRemove(const FSimEco_TradeStagedArray& In)  { if (In.OwnerComponent) In.OwnerComponent->HandleReplicatedChange(); }

namespace
{
	UObject* ResolveSeamOff(const AActor* Actor, TSubclassOf<UInterface> SeamClass)
	{
		if (!Actor || !*SeamClass) { return nullptr; }
		if (Actor->GetClass()->ImplementsInterface(SeamClass)) { return const_cast<AActor*>(Actor); }
		return const_cast<AActor*>(Actor)->FindComponentByInterface(SeamClass);
	}
}

USimEco_PlayerTradeComponent::USimEco_PlayerTradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	Staged.OwnerComponent = this;
}

void USimEco_PlayerTradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Owner-only: a trade offer is private to the trading player (the partner sees it through ITS own
	// component). Scalar state + the top-level staged fast-array.
	DOREPLIFETIME_CONDITION(USimEco_PlayerTradeComponent, Phase, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_PlayerTradeComponent, bConfirmed, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_PlayerTradeComponent, StagedCurrencyTag, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_PlayerTradeComponent, StagedCurrency, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USimEco_PlayerTradeComponent, Staged, COND_OwnerOnly);
}

void USimEco_PlayerTradeComponent::BeginPlay()
{
	Super::BeginPlay();
	// Re-bind the fast-array back-pointer: it is NotReplicated/Transient, so after net-deserialization
	// it is null and the PostReplicatedAdd/Change notifications would be lost. Matches USimEco_BankComponent.
	Staged.OwnerComponent = this;
}

void USimEco_PlayerTradeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// On disconnect/destroy, refund this side's escrow and tear the session down on both partners.
	if (HasAuthority() && Phase != ESimEco_TradePhase::Idle)
	{
		ServerEndSession(/*bRefund*/ true);
	}
	Super::EndPlay(EndPlayReason);
}

bool USimEco_PlayerTradeComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

UObject* USimEco_PlayerTradeComponent::ResolveOwnerInventoryRemove() const { return ResolveSeamOff(GetOwner(), USeam_TradableInventory::StaticClass()); }
UObject* USimEco_PlayerTradeComponent::ResolveOwnerInventoryAdd() const    { return ResolveSeamOff(GetOwner(), USeam_PurchaseTarget::StaticClass()); }
UObject* USimEco_PlayerTradeComponent::ResolveOwnerWallet() const          { return ResolveSeamOff(GetOwner(), USeam_WalletAuthority::StaticClass()); }

void USimEco_PlayerTradeComponent::OnRep_Session() { OnTradeChanged.Broadcast(this); }
void USimEco_PlayerTradeComponent::HandleReplicatedChange() { OnTradeChanged.Broadcast(this); }

void USimEco_PlayerTradeComponent::NotifyChanged()
{
	OnTradeChanged.Broadcast(this);
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_TradeChanged, FInstancedStruct(), GetOwner());
	}
}

// ----------------------------------------------------------------------------------------------------
// Client entry points
// ----------------------------------------------------------------------------------------------------

void USimEco_PlayerTradeComponent::RequestOpen(USimEco_PlayerTradeComponent* InPartner)
{
	if (HasAuthority()) { ServerOpen(InPartner); }
	else { Server_Open(InPartner); }
}
void USimEco_PlayerTradeComponent::RequestStageItem(FGameplayTag ItemTag, int32 Count)
{
	if (HasAuthority()) { ServerStageItem(ItemTag, Count); }
	else { Server_StageItem(ItemTag, Count); }
}
void USimEco_PlayerTradeComponent::RequestStageCurrency(FGameplayTag CurrencyTag, int64 Amount)
{
	if (HasAuthority()) { ServerStageCurrency(CurrencyTag, Amount); }
	else { Server_StageCurrency(CurrencyTag, Amount); }
}
void USimEco_PlayerTradeComponent::RequestConfirm()
{
	if (HasAuthority()) { ServerConfirm(); }
	else { Server_Confirm(); }
}
void USimEco_PlayerTradeComponent::RequestCancel()
{
	if (HasAuthority()) { ServerEndSession(/*bRefund*/ true); }
	else { Server_Cancel(); }
}

// ----------------------------------------------------------------------------------------------------
// Server RPCs
// ----------------------------------------------------------------------------------------------------

bool USimEco_PlayerTradeComponent::Server_Open_Validate(USimEco_PlayerTradeComponent* InPartner) { return InPartner != nullptr && InPartner != this; }
void USimEco_PlayerTradeComponent::Server_Open_Implementation(USimEco_PlayerTradeComponent* InPartner) { ServerOpen(InPartner); }

bool USimEco_PlayerTradeComponent::Server_StageItem_Validate(FGameplayTag ItemTag, int32 Count) { return ItemTag.IsValid() && Count > 0; }
void USimEco_PlayerTradeComponent::Server_StageItem_Implementation(FGameplayTag ItemTag, int32 Count) { ServerStageItem(ItemTag, Count); }

bool USimEco_PlayerTradeComponent::Server_StageCurrency_Validate(FGameplayTag CurrencyTag, int64 Amount) { return CurrencyTag.IsValid() && Amount >= 0; }
void USimEco_PlayerTradeComponent::Server_StageCurrency_Implementation(FGameplayTag CurrencyTag, int64 Amount) { ServerStageCurrency(CurrencyTag, Amount); }

bool USimEco_PlayerTradeComponent::Server_Confirm_Validate() { return true; }
void USimEco_PlayerTradeComponent::Server_Confirm_Implementation() { ServerConfirm(); }

bool USimEco_PlayerTradeComponent::Server_Cancel_Validate() { return true; }
void USimEco_PlayerTradeComponent::Server_Cancel_Implementation() { ServerEndSession(/*bRefund*/ true); }

// ----------------------------------------------------------------------------------------------------
// Server-side session logic
// ----------------------------------------------------------------------------------------------------

void USimEco_PlayerTradeComponent::ServerOpen(USimEco_PlayerTradeComponent* InPartner)
{
	if (!HasAuthority() || !InPartner || InPartner == this)
	{
		return;
	}
	// Both must be idle to start a clean session.
	if (Phase != ESimEco_TradePhase::Idle || InPartner->Phase != ESimEco_TradePhase::Idle)
	{
		return;
	}

	Partner = InPartner;
	InPartner->Partner = this;
	Phase = ESimEco_TradePhase::Negotiating;
	InPartner->Phase = ESimEco_TradePhase::Negotiating;
	bConfirmed = false;
	InPartner->bConfirmed = false;

	NotifyChanged();
	InPartner->NotifyChanged();
}

void USimEco_PlayerTradeComponent::ServerStageItem(const FGameplayTag& ItemTag, int32 Count)
{
	if (!HasAuthority() || Phase != ESimEco_TradePhase::Negotiating || Count <= 0 || !ItemTag.IsValid())
	{
		return;
	}

	// Escrow the item OUT of the owner's inventory into this component's staged array (anti-dupe).
	UObject* Inv = ResolveOwnerInventoryRemove();
	if (!Inv || !ISeam_TradableInventory::Execute_CanRemove(Inv, ItemTag, Count))
	{
		return;
	}
	const int32 Removed = ISeam_TradableInventory::Execute_RemoveItem(Inv, ItemTag, Count);
	if (Removed <= 0)
	{
		return;
	}

	// Merge into an existing staged line or add a new one.
	bool bMerged = false;
	for (FSimEco_TradeStagedItem& It : Staged.Items)
	{
		if (It.ItemTag == ItemTag)
		{
			It.Quantity += Removed;
			Staged.MarkItemDirty(It);
			bMerged = true;
			break;
		}
	}
	if (!bMerged)
	{
		FSimEco_TradeStagedItem& Added = Staged.Items.Add_GetRef(FSimEco_TradeStagedItem(ItemTag, Removed));
		Staged.MarkItemDirty(Added);
	}

	// ANY offer edit clears BOTH confirmations.
	ServerClearConfirmation();
	if (Partner.IsValid()) { Partner->ServerClearConfirmation(); }
	NotifyChanged();
}

void USimEco_PlayerTradeComponent::ServerStageCurrency(const FGameplayTag& CurrencyTag, int64 Amount)
{
	if (!HasAuthority() || Phase != ESimEco_TradePhase::Negotiating || Amount < 0 || !CurrencyTag.IsValid())
	{
		return;
	}

	UObject* Wallet = ResolveOwnerWallet();
	if (!Wallet)
	{
		return;
	}

	// First refund any previously-staged currency (we restage to an absolute amount).
	if (StagedCurrency > 0 && StagedCurrencyTag.IsValid())
	{
		ISeam_WalletAuthority::Execute_Grant(Wallet, StagedCurrencyTag, StagedCurrency);
		StagedCurrency = 0;
	}

	// Escrow the new amount OUT of the wallet.
	if (Amount > 0)
	{
		if (!ISeam_WalletAuthority::Execute_CanSpend(Wallet, CurrencyTag, Amount) ||
			!ISeam_WalletAuthority::Execute_Spend(Wallet, CurrencyTag, Amount))
		{
			NotifyChanged();
			return;
		}
		StagedCurrencyTag = CurrencyTag;
		StagedCurrency = Amount;
	}
	else
	{
		StagedCurrencyTag = FGameplayTag();
	}

	ServerClearConfirmation();
	if (Partner.IsValid()) { Partner->ServerClearConfirmation(); }
	NotifyChanged();
}

void USimEco_PlayerTradeComponent::ServerConfirm()
{
	if (!HasAuthority() || Phase != ESimEco_TradePhase::Negotiating)
	{
		return;
	}
	bConfirmed = true;
	NotifyChanged();
	TryCommit();
}

void USimEco_PlayerTradeComponent::ServerClearConfirmation()
{
	if (!HasAuthority())
	{
		return;
	}
	if (bConfirmed)
	{
		bConfirmed = false;
		NotifyChanged();
	}
}

void USimEco_PlayerTradeComponent::TryCommit()
{
	if (!HasAuthority() || !Partner.IsValid())
	{
		return;
	}

	// MUTUAL-EXCLUSION GATE: both partners' Server_Confirm RPCs can each invoke TryCommit on the same
	// server tick while Phase is still Negotiating. Without this guard both would pass the confirmation
	// check below and BOTH would deliver the escrow — duplicating items/currency. Only the first call to
	// flip Phase out of Negotiating proceeds; the second returns immediately. (Phase is server-side state;
	// the COND_OwnerOnly replication of it is irrelevant to this server-only gate.)
	if (Phase != ESimEco_TradePhase::Negotiating)
	{
		return;
	}

	USimEco_PlayerTradeComponent* P = Partner.Get();
	if (!(bConfirmed && P->bConfirmed))
	{
		return; // wait for both
	}

	Phase = ESimEco_TradePhase::Committing;
	P->Phase = ESimEco_TradePhase::Committing;

	// Atomic swap: each side receives the OTHER side's escrow. Escrow already left both inventories at
	// stage time, so delivery cannot dupe; a failed grant on one side is logged but the goods are not
	// lost (they remain conceptually owed — best-effort delivery, escrow is cleared after).
	P->ServerDeliverFrom(this);     // partner receives MY escrow
	this->ServerDeliverFrom(P);     // I receive partner's escrow

	// Both escrows are now delivered; end the session WITHOUT a refund (goods already moved).
	ServerEndSession(/*bRefund*/ false);
}

void USimEco_PlayerTradeComponent::ServerDeliverFrom(USimEco_PlayerTradeComponent* From)
{
	if (!HasAuthority() || !From)
	{
		return;
	}

	// Deliver From's staged ITEMS to THIS owner.
	if (UObject* AddInv = ResolveOwnerInventoryAdd())
	{
		for (const FSimEco_TradeStagedItem& It : From->Staged.Items)
		{
			ISeam_PurchaseTarget::Execute_GrantItem(AddInv, It.ItemTag, It.Quantity);
		}
	}
	// Deliver From's staged CURRENCY to THIS owner.
	if (From->StagedCurrency > 0 && From->StagedCurrencyTag.IsValid())
	{
		if (UObject* Wallet = ResolveOwnerWallet())
		{
			ISeam_WalletAuthority::Execute_Grant(Wallet, From->StagedCurrencyTag, From->StagedCurrency);
		}
	}
}

void USimEco_PlayerTradeComponent::ServerRefundEscrow()
{
	if (!HasAuthority())
	{
		return;
	}
	// Return staged items to the owner.
	if (UObject* AddInv = ResolveOwnerInventoryAdd())
	{
		for (const FSimEco_TradeStagedItem& It : Staged.Items)
		{
			ISeam_PurchaseTarget::Execute_GrantItem(AddInv, It.ItemTag, It.Quantity);
		}
	}
	// Return staged currency to the owner.
	if (StagedCurrency > 0 && StagedCurrencyTag.IsValid())
	{
		if (UObject* Wallet = ResolveOwnerWallet())
		{
			ISeam_WalletAuthority::Execute_Grant(Wallet, StagedCurrencyTag, StagedCurrency);
		}
	}
}

void USimEco_PlayerTradeComponent::ServerEndSession(bool bRefund)
{
	if (!HasAuthority())
	{
		return;
	}

	USimEco_PlayerTradeComponent* P = Partner.Get();

	if (bRefund)
	{
		ServerRefundEscrow();
		if (P) { P->ServerRefundEscrow(); }
	}

	// Reset THIS side.
	Staged.Items.Reset();
	Staged.MarkArrayDirty();
	StagedCurrency = 0;
	StagedCurrencyTag = FGameplayTag();
	bConfirmed = false;
	Phase = ESimEco_TradePhase::Idle;
	Partner = nullptr;
	NotifyChanged();

	// Reset the PARTNER side (if it still points back at us).
	if (P && P->Partner.Get() == this)
	{
		P->Staged.Items.Reset();
		P->Staged.MarkArrayDirty();
		P->StagedCurrency = 0;
		P->StagedCurrencyTag = FGameplayTag();
		P->bConfirmed = false;
		P->Phase = ESimEco_TradePhase::Idle;
		P->Partner = nullptr;
		P->NotifyChanged();
	}
}
