// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pricing/SimEco_PriceQuoteProvider.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Pricing/SimEco_EconomyReputation.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_EconomyReplicationProxy.h"
#include "Market/SimEco_EconomyTags.h"
#include "DesignPatternsSimEconomyModule.h"   // SimEcoNativeTags::Bus_PriceChanged
#include "Clock/Seam_SimClock.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

void USimEco_PriceQuoteProvider::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register as the cross-module price-quote service. WeakObserved: this is a world-lifetime object
	// held by a GameInstance-scoped locator, so the locator must NOT keep it alive across travel.
	if (UDP_ServiceLocatorSubsystem* Locator = ResolveLocator())
	{
		const bool bOk = Locator->RegisterService(
			SimEcoPricingTags::Service_PriceQuote, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
		bRegistered = bOk;
		UE_LOG(LogDP, Verbose, TEXT("[PriceQuoteProvider] Registered price-quote service: %s"),
			bOk ? TEXT("ok") : TEXT("FAILED"));
	}

	// On the server, sample price history from the market's PriceChanged bus notifications (one point
	// per sim day). The market already broadcasts this when a clearing price moves beyond the epsilon.
	if (HasWorldAuthority())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			TWeakObjectPtr<USimEco_PriceQuoteProvider> WeakThis(this);
			Bus->ListenNative(
				SimEcoNativeTags::Bus_PriceChanged,
				[WeakThis](const FDP_Message& Msg)
				{
					USimEco_PriceQuoteProvider* Self = WeakThis.Get();
					if (!Self)
					{
						return;
					}
					if (const FSimEco_PriceChangedMsg* Payload = Msg.Payload.GetPtr<FSimEco_PriceChangedMsg>())
					{
						Self->RecordPriceSample(Payload->CommodityTag, Self->ResolveCurrentDay(), Payload->NewPrice);
					}
				},
				this,
				EDP_MessageMatch::ExactOrChild);
		}
	}
}

int32 USimEco_PriceQuoteProvider::ResolveCurrentDay() const
{
	if (UDP_ServiceLocatorSubsystem* Locator = ResolveLocator())
	{
		if (UObject* ClockObj = Locator->ResolveService(SimEcoEconomyTags::Service_SimClock))
		{
			if (ClockObj->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
			{
				return ISeam_SimClock::Execute_GetDayNumber(ClockObj);
			}
		}
	}
	return 0;
}

void USimEco_PriceQuoteProvider::Deinitialize()
{
	if (bRegistered)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = ResolveLocator())
		{
			Locator->UnregisterService(SimEcoPricingTags::Service_PriceQuote);
		}
		bRegistered = false;
	}

	History.Reset();
	Super::Deinitialize();
}

UDP_ServiceLocatorSubsystem* USimEco_PriceQuoteProvider::ResolveLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

USimEco_MarketSubsystem* USimEco_PriceQuoteProvider::ResolveMarket() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
}

ASimEco_EconomyReplicationProxy* USimEco_PriceQuoteProvider::ResolveProxy() const
{
	if (USimEco_MarketSubsystem* Market = ResolveMarket())
	{
		return Market->GetReplicationProxy();
	}
	return nullptr;
}

double USimEco_PriceQuoteProvider::GetQuotedPrice_Implementation(FGameplayTag ItemOrCommodityTag) const
{
	if (!ItemOrCommodityTag.IsValid())
	{
		return 0.0;
	}

	// Server path: authoritative book via the market subsystem's public GetPrice.
	if (HasWorldAuthority())
	{
		if (USimEco_MarketSubsystem* Market = ResolveMarket())
		{
			return Market->GetPrice_Implementation(ItemOrCommodityTag);
		}
		return 0.0;
	}

	// Client path: the replicated price summary on the proxy (never the server-only book).
	if (const ASimEco_EconomyReplicationProxy* Proxy = ResolveProxy())
	{
		return Proxy->GetReplicatedPrice(ItemOrCommodityTag);
	}
	return 0.0;
}

float USimEco_PriceQuoteProvider::GetPriceMultiplierForFaction_Implementation(FGameplayTag FactionTag, const AActor* Buyer) const
{
	// This base provider is faction-neutral on the spread; the per-buyer reputation effect is folded
	// here so a shop UI that only has the price-quote seam still reflects standing. Concrete merchants
	// add their own spread/haggle via USimEco_MerchantComponent.
	if (!FactionTag.IsValid() || !Buyer)
	{
		return 1.0f;
	}

	float OutReputation = 0.0f;
	if (!FSimEco_EconomyReputation::TryGetReputation(this, Buyer, FactionTag, OutReputation))
	{
		// No reputation provider / no standing tracked: neutral.
		return 1.0f;
	}

	// Without a per-merchant discount curve here, derive a gentle, bounded standing discount: every
	// positive reputation point shaves a tiny fraction off, clamped. This is a defensive default — the
	// authored, designer-tuned discount lives on USimEco_PriceModifierDef and is applied by the merchant.
	const float NeutralMult = 1.0f;
	const float PerPointDiscount = 0.0005f;            // documented defensive fallback slope
	const float MinMult = 0.75f;                       // never more than 25% off via this fallback
	const float Mult = NeutralMult - (OutReputation * PerPointDiscount);
	return FMath::Clamp(Mult, MinMult, 1.0f);
}

void USimEco_PriceQuoteProvider::RecordPriceSample(const FGameplayTag& CommodityTag, int32 DayNumber, double Price)
{
	if (!CommodityTag.IsValid() || !HasWorldAuthority())
	{
		return;
	}

	FSimEco_PriceHistory& Hist = History.FindOrAdd(CommodityTag);

	// Same-day call overwrites the last sample so one day == one point.
	if (Hist.Samples.Num() > 0 && Hist.Samples.Last().DayNumber == DayNumber)
	{
		Hist.Samples.Last().Price = Price;
	}
	else
	{
		FSimEco_PriceSample Sample;
		Sample.DayNumber = DayNumber;
		Sample.Price = Price;
		Hist.Samples.Add(Sample);
	}

	// Trim to the bounded ring length.
	const int32 MaxLen = FMath::Max(2, MaxHistoryLength);
	if (Hist.Samples.Num() > MaxLen)
	{
		// Drop the oldest samples. Use the 2-arg RemoveAt (count) form, available across UE 5.3-5.5
		// (the 3-arg EAllowShrinking overload is 5.5+ only).
		Hist.Samples.RemoveAt(0, Hist.Samples.Num() - MaxLen);
	}
}

void USimEco_PriceQuoteProvider::GetPriceHistory(FGameplayTag CommodityTag, TArray<FSimEco_PriceSample>& OutSamples) const
{
	OutSamples.Reset();
	if (const FSimEco_PriceHistory* Hist = History.Find(CommodityTag))
	{
		OutSamples = Hist->Samples;
	}
}

float USimEco_PriceQuoteProvider::GetPriceTrend(FGameplayTag CommodityTag) const
{
	const FSimEco_PriceHistory* Hist = History.Find(CommodityTag);
	if (!Hist || Hist->Samples.Num() < 2)
	{
		return 0.0f;
	}

	const double First = Hist->Samples[0].Price;
	const double Last = Hist->Samples.Last().Price;
	if (First <= 0.0)
	{
		return 0.0f;
	}

	const double Frac = (Last - First) / First;
	// Map a >=5% change to full +/-1; smaller changes scale linearly. Defensive flat band of 5%.
	return FMath::Clamp((float)(Frac / 0.05), -1.0f, 1.0f);
}

FString USimEco_PriceQuoteProvider::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("PriceQuoteProvider auth=%d histCommodities=%d registered=%d"),
		HasWorldAuthority() ? 1 : 0, History.Num(), bRegistered ? 1 : 0);
}
