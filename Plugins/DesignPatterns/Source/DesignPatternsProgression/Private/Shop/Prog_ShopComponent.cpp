// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shop/Prog_ShopComponent.h"
#include "Shop/Prog_ShopDefinition.h"
#include "Shop/Prog_ShopBusPayloads.h"
#include "Achievement/Prog_Condition.h"
#include "Wallet/Prog_WalletComponent.h"
#include "DesignPatternsProgressionModule.h"
#include "Economy/Seam_Wallet.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ FProg_ShopStockEntry replication callbacks (client side) -----------------------------------

void FProg_ShopStockEntry::PostReplicatedAdd(const FProg_ShopStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedStockChange();
	}
}

void FProg_ShopStockEntry::PostReplicatedChange(const FProg_ShopStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedStockChange();
	}
}

void FProg_ShopStockEntry::PreReplicatedRemove(const FProg_ShopStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedStockChange();
	}
}

//~ UProg_ShopComponent -----------------------------------------------------------------------

UProg_ShopComponent::UProg_ShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and clients).
	Stock.OwnerComponent = this;
}

void UProg_ShopComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UProg_ShopComponent, Stock);
}

void UProg_ShopComponent::BeginPlay()
{
	Super::BeginPlay();

	// Seed authoritative stock once, on the server only. Clients receive it via replication.
	if (GetOwner() && GetOwner()->HasAuthority() && !bStockSeeded)
	{
		SeedStockFromDefinition();
	}
}

void UProg_ShopComponent::SeedStockFromDefinition()
{
	// AUTHORITY GUARD: stock is replicated server-authoritative state.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	Stock.Entries.Reset();

	if (ShopDefinition)
	{
		for (int32 Index = 0; Index < ShopDefinition->Entries.Num(); ++Index)
		{
			const FProg_ShopEntry& Entry = ShopDefinition->Entries[Index];
			// Only finite-stock entries get a replicated counter; infinite-stock entries are absent.
			if (Entry.HasFiniteStock())
			{
				FProg_ShopStockEntry& StockEntry =
					Stock.Entries.Add_GetRef(FProg_ShopStockEntry(Index, FMath::Max(0, Entry.Stock)));
				Stock.MarkItemDirty(StockEntry);
			}
		}
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[Prog_Shop] %s seeded with no ShopDefinition; shop is empty."),
			*GetNameSafe(GetOwner()));
	}

	Stock.MarkArrayDirty();
	bStockSeeded = true;
	NotifyStockChanged();
}

FProg_ShopStockEntry* UProg_ShopComponent::FindStockEntry(int32 EntryIndex)
{
	return Stock.Entries.FindByPredicate(
		[EntryIndex](const FProg_ShopStockEntry& E) { return E.EntryIndex == EntryIndex; });
}

const FProg_ShopStockEntry* UProg_ShopComponent::FindStockEntry(int32 EntryIndex) const
{
	return Stock.Entries.FindByPredicate(
		[EntryIndex](const FProg_ShopStockEntry& E) { return E.EntryIndex == EntryIndex; });
}

int32 UProg_ShopComponent::GetRemainingStock(int32 EntryIndex) const
{
	if (!ShopDefinition || !ShopDefinition->IsValidEntryIndex(EntryIndex))
	{
		return 0;
	}
	// Infinite-stock entries report -1 (no counter present).
	if (!ShopDefinition->Entries[EntryIndex].HasFiniteStock())
	{
		return -1;
	}
	if (const FProg_ShopStockEntry* Found = FindStockEntry(EntryIndex))
	{
		return Found->Remaining;
	}
	// Finite entry with no counter yet (pre-seed on a freshly-replicated client): treat as sold out
	// conservatively rather than letting a purchase through against unknown stock.
	return 0;
}

bool UProg_ShopComponent::IsInStock(int32 EntryIndex) const
{
	const int32 Remaining = GetRemainingStock(EntryIndex);
	return Remaining < 0 || Remaining > 0;
}

void UProg_ShopComponent::GetOffers(TArray<FProg_ShopOffer>& OutOffers) const
{
	OutOffers.Reset();
	if (!ShopDefinition)
	{
		return;
	}

	OutOffers.Reserve(ShopDefinition->Entries.Num());
	for (int32 Index = 0; Index < ShopDefinition->Entries.Num(); ++Index)
	{
		const FProg_ShopEntry& Entry = ShopDefinition->Entries[Index];

		FProg_ShopOffer Offer;
		Offer.EntryIndex = Index;
		Offer.ItemTag = Entry.ItemTag;
		Offer.GrantCount = Entry.GrantCount;
		Offer.PriceCurrency = Entry.PriceCurrency;
		Offer.Price = Entry.Price;
		Offer.Remaining = GetRemainingStock(Index);
		Offer.bSoldOut = Entry.HasFiniteStock() && Offer.Remaining == 0;
		Offer.bHasUnlockGate = Entry.UnlockCondition != nullptr;
		OutOffers.Add(Offer);
	}
}

bool UProg_ShopComponent::EvaluateEntryUnlock_Implementation(const AActor* Buyer, int32 EntryIndex) const
{
	// Fail closed on a bad buyer or index.
	if (!Buyer || !ShopDefinition || !ShopDefinition->IsValidEntryIndex(EntryIndex))
	{
		return false;
	}

	const FProg_ShopEntry& Entry = ShopDefinition->Entries[EntryIndex];

	// No gate authored -> the entry is always unlocked. When a gate IS authored, evaluate it for real
	// against the world context (the condition reads the World hub / counters via its own seam). This is
	// FAIL-CLOSED: if the gate cannot be evaluated (e.g. a null world context) it stays locked, so an
	// authored unlock condition can never be bypassed. Projects extend behaviour by subclassing
	// UProg_Condition, not by overriding this method.
	if (Entry.UnlockCondition == nullptr)
	{
		return true;
	}
	const UObject* WorldContext = GetOwner();
	if (!WorldContext)
	{
		return false; // fail-closed: cannot evaluate -> treat as locked
	}
	return Entry.UnlockCondition->Evaluate(const_cast<UObject*>(WorldContext));
}

UObject* UProg_ShopComponent::ResolveSeamObject(const AActor* Buyer, TSubclassOf<UInterface> SeamClass)
{
	if (!Buyer || !SeamClass)
	{
		return nullptr;
	}

	// 1) The actor itself implements the seam.
	if (Buyer->GetClass()->ImplementsInterface(SeamClass))
	{
		return const_cast<AActor*>(Buyer);
	}

	// 2) One of its components implements the seam.
	if (UActorComponent* Comp = const_cast<AActor*>(Buyer)->FindComponentByInterface(SeamClass))
	{
		return Comp;
	}

	return nullptr;
}

UProg_WalletComponent* UProg_ShopComponent::ResolveBuyerWallet(const AActor* Buyer)
{
	if (!Buyer)
	{
		return nullptr;
	}
	// The wallet component IS the ISeam_Wallet implementer (same module), so resolve by the seam and
	// down-cast: this also tolerates a project subclass of UProg_WalletComponent.
	if (UObject* WalletObj = ResolveSeamObject(Buyer, USeam_Wallet::StaticClass()))
	{
		return Cast<UProg_WalletComponent>(WalletObj);
	}
	return nullptr;
}

EProg_PurchaseResult UProg_ShopComponent::TryPurchase(AActor* Buyer, int32 EntryIndex)
{
	// AUTHORITY GUARD at the very top: clients never run the purchase flow.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return EProg_PurchaseResult::NotAuthoritative;
	}

	if (!ShopDefinition)
	{
		return EProg_PurchaseResult::NoShop;
	}
	if (!ShopDefinition->IsValidEntryIndex(EntryIndex) || !Buyer)
	{
		return EProg_PurchaseResult::BadEntry;
	}

	const FProg_ShopEntry& Entry = ShopDefinition->Entries[EntryIndex];
	if (!Entry.IsValidEntry())
	{
		return EProg_PurchaseResult::BadEntry;
	}

	// Helper to publish the outcome on the bus and return.
	auto Finish = [this, Buyer, EntryIndex, &Entry](EProg_PurchaseResult Result, int32 GrantedCount, int64 PricePaid)
		-> EProg_PurchaseResult
	{
		if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FProg_ShopPurchaseEvent Event;
			Event.Vendor = GetOwner();
			Event.Buyer = Buyer;
			Event.EntryIndex = EntryIndex;
			Event.ItemTag = Entry.ItemTag;
			Event.GrantedCount = GrantedCount;
			Event.PriceCurrency = Entry.PriceCurrency;
			Event.PricePaid = PricePaid;
			Event.bSuccess = (Result == EProg_PurchaseResult::Success);
			Bus->BroadcastPayload(ProgTags::Bus_ShopPurchased, FInstancedStruct::Make(Event), this);
		}
		return Result;
	};

	// 1) Unlock gate.
	if (!EvaluateEntryUnlock(Buyer, EntryIndex))
	{
		return Finish(EProg_PurchaseResult::Locked, 0, 0);
	}

	// 2) Finite stock.
	if (Entry.HasFiniteStock() && GetRemainingStock(EntryIndex) <= 0)
	{
		return Finish(EProg_PurchaseResult::OutOfStock, 0, 0);
	}

	// 3) Affordability (paid entries only). Read through the read-only ISeam_Wallet seam, and resolve
	//    the concrete wallet component for the atomic debit below.
	const bool bIsPaid = Entry.Price > 0 && Entry.PriceCurrency.IsValid();
	UProg_WalletComponent* Wallet = nullptr;
	if (bIsPaid)
	{
		UObject* WalletObj = ResolveSeamObject(Buyer, USeam_Wallet::StaticClass());
		if (!WalletObj)
		{
			return Finish(EProg_PurchaseResult::NoWallet, 0, 0);
		}
		if (!ISeam_Wallet::Execute_CanAfford(WalletObj, Entry.PriceCurrency, Entry.Price))
		{
			return Finish(EProg_PurchaseResult::CannotAfford, 0, 0);
		}
		// Concrete component needed for the authoritative SpendCurrency. A buyer whose wallet is a
		// project adapter that is NOT a UProg_WalletComponent cannot be debited atomically here; treat
		// that as NoWallet rather than silently granting a free item.
		Wallet = ResolveBuyerWallet(Buyer);
		if (!Wallet)
		{
			UE_LOG(LogDP, Warning,
				TEXT("[Prog_Shop] Buyer %s wallet is not a UProg_WalletComponent; cannot debit."),
				*GetNameSafe(Buyer));
			return Finish(EProg_PurchaseResult::NoWallet, 0, 0);
		}
	}

	// 4) Delivery target must be able to receive the item BEFORE we debit anything.
	UObject* TargetObj = ResolveSeamObject(Buyer, USeam_PurchaseTarget::StaticClass());
	if (!TargetObj)
	{
		return Finish(EProg_PurchaseResult::NoPurchaseTarget, 0, 0);
	}
	if (!ISeam_PurchaseTarget::Execute_CanReceive(TargetObj, Entry.ItemTag, Entry.GrantCount))
	{
		return Finish(EProg_PurchaseResult::TargetRejected, 0, 0);
	}

	// 5) Debit currency atomically (authority-only SpendCurrency; re-checks balance, never goes
	//    negative). Done before the grant so the spend is the gating commit.
	if (bIsPaid)
	{
		if (!Wallet->SpendCurrency(Entry.PriceCurrency, Entry.Price))
		{
			// Balance changed between CanAfford and here (e.g. a concurrent spend). Not an error.
			return Finish(EProg_PurchaseResult::CannotAfford, 0, 0);
		}
	}

	// 6) Grant the item.
	const int32 Granted = ISeam_PurchaseTarget::Execute_GrantItem(TargetObj, Entry.ItemTag, Entry.GrantCount);
	if (Granted <= 0)
	{
		// Delivery failed after the CanReceive pre-check (race / capacity change). REFUND the debit so
		// the buyer is never charged for an item they did not receive — this is why the wallet is
		// resolved concretely rather than spent through a fire-and-forget message.
		if (bIsPaid && Wallet)
		{
			Wallet->AddCurrency(Entry.PriceCurrency, Entry.Price);
		}
		UE_LOG(LogDP, Warning, TEXT("[Prog_Shop] GrantItem returned 0 for %s after CanReceive passed; refunded."),
			*Entry.ItemTag.ToString());
		return Finish(EProg_PurchaseResult::TargetRejected, 0, 0);
	}

	// 7) Decrement finite stock and replicate the change.
	if (Entry.HasFiniteStock())
	{
		if (FProg_ShopStockEntry* StockEntry = FindStockEntry(EntryIndex))
		{
			StockEntry->Remaining = FMath::Max(0, StockEntry->Remaining - 1);
			Stock.MarkItemDirty(*StockEntry);
			NotifyStockChanged();
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[Prog_Shop] %s bought %d x %s from %s for %lld %s."),
		*GetNameSafe(Buyer), Granted, *Entry.ItemTag.ToString(), *GetNameSafe(GetOwner()),
		bIsPaid ? Entry.Price : 0, *Entry.PriceCurrency.ToString());

	return Finish(EProg_PurchaseResult::Success, Granted, bIsPaid ? Entry.Price : 0);
}

void UProg_ShopComponent::HandleReplicatedStockChange()
{
	// Surfaced on clients by the fast-array callbacks; broadcast so bound UI refreshes its offers.
	NotifyStockChanged();
}

void UProg_ShopComponent::NotifyStockChanged()
{
	OnShopStockChanged.Broadcast(this);
}
