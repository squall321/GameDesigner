// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pricing/SimEco_PriceModifierDef.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

USimEco_PriceModifierDef::USimEco_PriceModifierDef()
{
	// Defaults are set inline on the UPROPERTYs; nothing to do here. Constructor exists so each asset
	// instance is fully initialised even before a designer touches the values.
}

FName USimEco_PriceModifierDef::GetDataAssetType_Implementation() const
{
	// Collapse all price-modifier assets into one asset-manager bucket so a project can scan/preload
	// the whole pricing sheet family by a single PrimaryAssetType.
	return FName(TEXT("SimEco_PriceModifier"));
}

float USimEco_PriceModifierDef::EvaluateReputationMultiplier(float Reputation, ESimEco_TradeSide Side) const
{
	if (!ReputationDiscountCurve)
	{
		return 1.0f;
	}

	// The curve is authored for the BUY (ask) side: standing -> a discount multiplier (<= 1).
	const float BuyMult = ReputationDiscountCurve->GetFloatValue(Reputation);

	if (Side == ESimEco_TradeSide::PlayerBuy)
	{
		// Defensive: a discount should not push price up; clamp to (0, 1].
		return FMath::Clamp(BuyMult, KINDA_SMALL_NUMBER, 1.0f);
	}

	// Player SELL: mirror the discount into a bonus. A 0.8 buy-discount (20% off) maps to a 1.2 sell
	// bonus, clamped so high standing can't make a sell exceed base by more than MaxSellRepBonus.
	const float SellBonus = 2.0f - BuyMult;
	return FMath::Clamp(SellBonus, 1.0f, FMath::Max(1.0f, MaxSellRepBonus));
}

float USimEco_PriceModifierDef::EvaluateScarcityMultiplier(float ScarcityRatio) const
{
	if (!ScarcityToMarkupCurve)
	{
		return 1.0f;
	}

	const float Clamped = FMath::Clamp(ScarcityRatio, 0.0f, FMath::Max(1.0f, MaxScarcityRatio));
	const float Mult = ScarcityToMarkupCurve->GetFloatValue(Clamped);

	// A scarcity curve should never discount; clamp to >= 1 defensively.
	return FMath::Max(1.0f, Mult);
}

double USimEco_PriceModifierDef::ComputeEffectivePrice(double BasePrice, ESimEco_TradeSide Side,
	float Reputation, float ScarcityRatio, float HaggleMultiplier) const
{
	if (BasePrice <= 0.0 || !FMath::IsFinite(BasePrice))
	{
		return 0.0;
	}

	// 1) Regional context.
	double Price = BasePrice * FMath::Max(0.0f, RegionalMultiplier);

	// 2) The spread (merchant margin) per side.
	if (Side == ESimEco_TradeSide::PlayerBuy)
	{
		Price *= (1.0 + FMath::Max(0.0f, BaseMarkupFraction));
	}
	else
	{
		Price *= (1.0 - FMath::Clamp(BaseMarkdownFraction, 0.0f, 0.99f));
	}

	// 3) Scarcity markup (only meaningfully applies to a buy, but symmetric is harmless on sell because
	//    a short merchant also values the player's goods more — designers can flatten the curve to 1).
	Price *= EvaluateScarcityMultiplier(ScarcityRatio);

	// 4) Reputation.
	Price *= EvaluateReputationMultiplier(Reputation, Side);

	// 5) Haggling — only on a buy, and only ever a discount, clamped to the authored floor.
	if (Side == ESimEco_TradeSide::PlayerBuy)
	{
		const float ClampedHaggle = FMath::Clamp(HaggleMultiplier,
			FMath::Clamp(MinHaggleMultiplier, 0.0f, 1.0f), 1.0f);
		Price *= ClampedHaggle;
	}

	// 6) Final hard clamp to [floor, ceiling] fraction of base.
	const double FloorPrice = BasePrice * FMath::Max(0.0f, PriceFloorFraction);
	const double CeilPrice = BasePrice * FMath::Max(1.0f, PriceCeilingFraction);
	Price = FMath::Clamp(Price, FloorPrice, CeilPrice);

	return Price;
}

#if WITH_EDITOR
EDataValidationResult USimEco_PriceModifierDef::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (PriceCeilingFraction < PriceFloorFraction)
	{
		Context.AddError(FText::FromString(TEXT(
			"SimEco_PriceModifierDef: PriceCeilingFraction must be >= PriceFloorFraction.")));
		Result = EDataValidationResult::Invalid;
	}
	if (BaseMarkdownFraction >= 1.0f)
	{
		Context.AddError(FText::FromString(TEXT(
			"SimEco_PriceModifierDef: BaseMarkdownFraction must be < 1.0 (a sell can't pay >= base).")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
