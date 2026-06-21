// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shop/Prog_ShopPricingDecorator.h"
#include "Shop/Prog_ShopComponent.h"
#include "Economy/Seam_PriceQuote.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * The service-locator key the SimEconomy price-quote provider registers under. Resolved BY NAME so
	 * Progression never includes the SimEconomy module that defines the native tag (cross-module
	 * decoupling). If the tag table has no such tag, this returns an invalid tag and pricing falls back.
	 */
	FGameplayTag GetPriceQuoteServiceKey()
	{
		// ErrorIfNotFound=false: a project without the economy module simply has no such service.
		return FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Eco.PriceQuote")), /*ErrorIfNotFound*/ false);
	}
}

UProg_ShopPricingDecorator::UProg_ShopPricingDecorator()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UProg_ShopPricingDecorator::BeginPlay()
{
	Super::BeginPlay();
	if (!Shop)
	{
		// Compose the sibling shop component on the same vendor actor.
		if (AActor* Owner = GetOwner())
		{
			Shop = Owner->FindComponentByClass<UProg_ShopComponent>();
		}
	}
}

UObject* UProg_ShopPricingDecorator::ResolvePriceQuoteProvider() const
{
	const FGameplayTag Key = GetPriceQuoteServiceKey();
	if (!Key.IsValid())
	{
		return nullptr;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		UObject* Provider = Locator->ResolveService(Key);
		if (Provider && Provider->GetClass()->ImplementsInterface(USeam_PriceQuote::StaticClass()))
		{
			return Provider;
		}
	}
	return nullptr;
}

int64 UProg_ShopPricingDecorator::GetLivePriceForOffer(const AActor* Buyer, const FProg_ShopOffer& Offer) const
{
	// Start from the shop's authored static price (the guaranteed fallback).
	double Base = (double)Offer.Price;

	UObject* Provider = ResolvePriceQuoteProvider();
	if (Provider)
	{
		if (bPreferLiveQuote)
		{
			const double LiveQuote = ISeam_PriceQuote::Execute_GetQuotedPrice(Provider, Offer.ItemTag);
			if (LiveQuote > 0.0)
			{
				Base = LiveQuote;
			}
		}
		// Reputation / faction multiplier applies regardless of which base we used.
		const float Mult = ISeam_PriceQuote::Execute_GetPriceMultiplierForFaction(Provider, FactionTag, Buyer);
		Base *= FMath::Max(0.0f, Mult);
	}

	// Currency is whole units; round and floor at 0 (a free entry stays free).
	const int64 Rounded = (int64)FMath::RoundToDouble(Base);
	return FMath::Max((int64)0, Rounded);
}

void UProg_ShopPricingDecorator::GetPricedOffers(const AActor* Buyer, TArray<FProg_ShopOffer>& OutOffers) const
{
	OutOffers.Reset();
	if (!Shop)
	{
		return;
	}

	// Pull the static offers, then overwrite each price with the live, reputation-adjusted one.
	Shop->GetOffers(OutOffers);
	for (FProg_ShopOffer& Offer : OutOffers)
	{
		Offer.Price = GetLivePriceForOffer(Buyer, Offer);
	}
}

void UProg_ShopPricingDecorator::NotifyPricesMayHaveChanged()
{
	OnPricedOffersChanged.Broadcast(Shop);
}
