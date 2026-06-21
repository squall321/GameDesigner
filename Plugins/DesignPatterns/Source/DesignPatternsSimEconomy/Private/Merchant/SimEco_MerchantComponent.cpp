// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Merchant/SimEco_MerchantComponent.h"
#include "Pricing/SimEco_EconomyReputation.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_Market.h"
#include "Economy/SimEco_EconomySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// ----------------------------------------------------------------------------------------------------
// Fast-array item callbacks (clients): forward to the owning component so UI refreshes after replication.
// ----------------------------------------------------------------------------------------------------

void FSimEco_MerchantStockEntry::PostReplicatedAdd(const FSimEco_MerchantStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent) { InArraySerializer.OwnerComponent->HandleReplicatedStockChange(); }
}
void FSimEco_MerchantStockEntry::PostReplicatedChange(const FSimEco_MerchantStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent) { InArraySerializer.OwnerComponent->HandleReplicatedStockChange(); }
}
void FSimEco_MerchantStockEntry::PreReplicatedRemove(const FSimEco_MerchantStockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent) { InArraySerializer.OwnerComponent->HandleReplicatedStockChange(); }
}

// ----------------------------------------------------------------------------------------------------

USimEco_MerchantComponent::USimEco_MerchantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	MerchantStock.OwnerComponent = this;
}

void USimEco_MerchantComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimEco_MerchantComponent, MerchantStock);
}

void USimEco_MerchantComponent::BeginPlay()
{
	Super::BeginPlay();
	// The back-pointer must be valid on BOTH server and client (clients fire entry callbacks).
	MerchantStock.OwnerComponent = this;

	if (HasAuthority())
	{
		if (!bSeeded)
		{
			SeedStockFromCatalogue();
		}

		// Register with the economy driver so AdvanceEconomyStep is called each fixed step.
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

void USimEco_MerchantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

bool USimEco_MerchantComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_MarketSubsystem* USimEco_MerchantComponent::ResolveMarket() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
}

USimEco_EconomySubsystem* USimEco_MerchantComponent::ResolveEconomy() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this);
}

FGameplayTag USimEco_MerchantComponent::GetEffectiveFactionTag() const
{
	if (MerchantFactionTag.IsValid())
	{
		return MerchantFactionTag;
	}
	return PriceModifier ? PriceModifier->MerchantFactionTag : FGameplayTag();
}

int32 USimEco_MerchantComponent::FindStockIndex(const FGameplayTag& CommodityTag) const
{
	for (int32 i = 0; i < MerchantStock.Entries.Num(); ++i)
	{
		if (MerchantStock.Entries[i].CommodityTag == CommodityTag)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void USimEco_MerchantComponent::SeedStockFromCatalogue()
{
	if (!HasAuthority())
	{
		return;
	}

	MerchantStock.Entries.Reset();
	for (const FSimEco_MerchantCatalogueLine& Line : Catalogue)
	{
		if (!Line.CommodityTag.IsValid())
		{
			continue;
		}
		FSimEco_MerchantStockEntry Entry;
		Entry.CommodityTag = Line.CommodityTag;
		Entry.Stock = FMath::Max(0, Line.InitialStock);
		Entry.RestockTarget = FMath::Max(0, Line.RestockTarget);
		Entry.RestockPerCycle = FMath::Max(0, Line.RestockPerCycle);
		MerchantStock.Entries.Add(Entry);
	}
	MerchantStock.MarkArrayDirty();
	bSeeded = true;
	NotifyStockChanged();
}

void USimEco_MerchantComponent::RestockNow()
{
	if (!HasAuthority())
	{
		return;
	}

	bool bChanged = false;
	for (FSimEco_MerchantStockEntry& Entry : MerchantStock.Entries)
	{
		if (Entry.RestockTarget <= 0 || Entry.RestockPerCycle <= 0)
		{
			continue;
		}
		if (Entry.Stock < Entry.RestockTarget)
		{
			const int32 NewStock = FMath::Min(Entry.RestockTarget, Entry.Stock + Entry.RestockPerCycle);
			if (NewStock != Entry.Stock)
			{
				Entry.Stock = NewStock;
				MerchantStock.MarkItemDirty(Entry);
				bChanged = true;
			}
		}
	}
	if (bChanged)
	{
		NotifyStockChanged();
	}
}

void USimEco_MerchantComponent::AdvanceEconomyStep(double /*StepSeconds*/, int64 /*StepIndex*/, int32 /*DayNumber*/)
{
	// Called on the server only by the economy driver. Authority is implied but guard defensively.
	if (!HasAuthority())
	{
		return;
	}

	const int32 Cadence = FMath::Max(0, RestockEveryNSteps);
	if (Cadence == 0 || ++StepsSinceRestock >= Cadence)
	{
		StepsSinceRestock = 0;
		RestockNow();
	}
}

int32 USimEco_MerchantComponent::ConsumeStockForSale(const FGameplayTag& CommodityTag, int32 Count)
{
	if (!HasAuthority() || Count <= 0)
	{
		return 0;
	}
	const int32 Idx = FindStockIndex(CommodityTag);
	if (Idx == INDEX_NONE)
	{
		return 0;
	}
	FSimEco_MerchantStockEntry& Entry = MerchantStock.Entries[Idx];
	const int32 Removed = FMath::Min(Count, Entry.Stock);
	if (Removed > 0)
	{
		Entry.Stock -= Removed;
		MerchantStock.MarkItemDirty(Entry);
		NotifyStockChanged();
	}
	return Removed;
}

int32 USimEco_MerchantComponent::AddStockFromPurchase(const FGameplayTag& CommodityTag, int32 Count)
{
	if (!HasAuthority() || Count <= 0 || !CommodityTag.IsValid())
	{
		return 0;
	}
	int32 Idx = FindStockIndex(CommodityTag);
	if (Idx == INDEX_NONE)
	{
		FSimEco_MerchantStockEntry NewEntry;
		NewEntry.CommodityTag = CommodityTag;
		Idx = MerchantStock.Entries.Add(NewEntry);
	}
	FSimEco_MerchantStockEntry& Entry = MerchantStock.Entries[Idx];
	Entry.Stock += Count;
	MerchantStock.MarkItemDirty(Entry);
	NotifyStockChanged();
	return Count;
}

bool USimEco_MerchantComponent::DealsIn(FGameplayTag CommodityTag) const
{
	if (FindStockIndex(CommodityTag) != INDEX_NONE)
	{
		return true;
	}
	for (const FSimEco_MerchantCatalogueLine& Line : Catalogue)
	{
		if (Line.CommodityTag == CommodityTag)
		{
			return true;
		}
	}
	return false;
}

int32 USimEco_MerchantComponent::GetStock(FGameplayTag CommodityTag) const
{
	const int32 Idx = FindStockIndex(CommodityTag);
	return (Idx != INDEX_NONE) ? MerchantStock.Entries[Idx].Stock : 0;
}

int64 USimEco_MerchantComponent::QuoteUnitPrice(FGameplayTag CommodityTag, ESimEco_TradeSide Side,
	const AActor* Buyer, float HaggleMultiplier) const
{
	if (!CommodityTag.IsValid())
	{
		return 0;
	}

	// Base price from the market (public surface only).
	double BasePrice = 0.0;
	if (USimEco_MarketSubsystem* Market = ResolveMarket())
	{
		BasePrice = Market->GetPrice_Implementation(CommodityTag);
	}
	if (BasePrice <= 0.0)
	{
		return 0;
	}

	// Resolve the buyer's reputation with this merchant's faction (fails closed to neutral 0).
	float Reputation = 0.0f;
	const FGameplayTag Faction = GetEffectiveFactionTag();
	if (Faction.IsValid() && Buyer)
	{
		FSimEco_EconomyReputation::TryGetReputation(this, Buyer, Faction, Reputation);
	}

	// Scarcity ratio for a player-BUY: rises as on-hand stock falls toward (and below) the restock
	// target, so a near-empty merchant prices an item dearer. 1.0 = at/above target (no scarcity
	// effect). The actual non-linear response curve is authored on the price-modifier asset; this just
	// produces the ratio fed into it. Defaults to 1.0 for a sell or when no target is configured.
	float ScarcityRatio = 1.0f;
	if (Side == ESimEco_TradeSide::PlayerBuy)
	{
		const int32 Idx = FindStockIndex(CommodityTag);
		if (Idx != INDEX_NONE)
		{
			const FSimEco_MerchantStockEntry& Entry = MerchantStock.Entries[Idx];
			const float Target = (float)FMath::Max(1, Entry.RestockTarget);
			const float OnHand = (float)FMath::Max(0, Entry.Stock);
			// ratio = target / (onHand + 1): equals ~1 at target, grows as stock drops toward 0.
			ScarcityRatio = Target / (OnHand + 1.0f);
		}
	}

	double Effective = BasePrice;
	if (PriceModifier)
	{
		Effective = PriceModifier->ComputeEffectivePrice(BasePrice, Side, Reputation, ScarcityRatio, HaggleMultiplier);
	}
	else
	{
		// No modifier asset: apply only a documented defensive spread so buy != sell.
		Effective = (Side == ESimEco_TradeSide::PlayerBuy) ? BasePrice * 1.2 : BasePrice * 0.6;
	}

	// Currency is whole units; round to nearest, floor at 1 for a non-free item.
	const int64 Rounded = (int64)FMath::RoundToDouble(Effective);
	return FMath::Max((int64)1, Rounded);
}

void USimEco_MerchantComponent::NotifyStockChanged()
{
	OnMerchantStockChanged.Broadcast(this);
}

void USimEco_MerchantComponent::HandleReplicatedStockChange()
{
	OnMerchantStockChanged.Broadcast(this);
}
