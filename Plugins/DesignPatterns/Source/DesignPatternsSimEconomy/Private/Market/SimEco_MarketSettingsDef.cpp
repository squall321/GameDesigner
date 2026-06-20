// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Market/SimEco_MarketSettingsDef.h"
#include "DesignPatternsSimEconomyModule.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

USimEco_MarketSettings::USimEco_MarketSettings()
{
	// Anchor the asset's identity tag under the commodity-agnostic market root by default so an
	// authored asset without an explicit DataTag still validates against a sensible hierarchy.
	DataTag = FGameplayTag();
}

FName USimEco_MarketSettings::GetDataAssetType_Implementation() const
{
	return FName(TEXT("SimEco.MarketSettings"));
}

const FSimEco_CommodityPriceRule* USimEco_MarketSettings::FindRule(const FGameplayTag& CommodityTag) const
{
	if (!CommodityTag.IsValid())
	{
		return nullptr;
	}
	return CommodityRules.FindByPredicate(
		[&CommodityTag](const FSimEco_CommodityPriceRule& Rule)
		{
			return Rule.CommodityTag == CommodityTag;
		});
}

double USimEco_MarketSettings::GetBasePriceForDay(FGameplayTag CommodityTag, int32 DayNumber) const
{
	const FSimEco_CommodityPriceRule* Rule = FindRule(CommodityTag);
	if (!Rule)
	{
		return 0.0;
	}

	double Base = Rule->BasePrice;
	if (Rule->BasePriceByDayCurve)
	{
		Base = static_cast<double>(Rule->BasePriceByDayCurve->GetFloatValue(static_cast<float>(DayNumber)));
	}

	return FMath::Clamp(Base, Rule->PriceFloor, Rule->PriceCeiling);
}

#if WITH_EDITOR
EDataValidationResult USimEco_MarketSettings::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (ImbalanceNormalization <= 0.0)
	{
		Context.AddError(NSLOCTEXT("SimEco", "BadImbalanceNorm",
			"ImbalanceNormalization must be > 0."));
		Result = EDataValidationResult::Invalid;
	}

	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < CommodityRules.Num(); ++Index)
	{
		const FSimEco_CommodityPriceRule& Rule = CommodityRules[Index];
		if (!Rule.CommodityTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("SimEco", "RuleNoTag", "CommodityRules[{0}] has no CommodityTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Rule.PriceCeiling < Rule.PriceFloor)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("SimEco", "RuleBadBand", "CommodityRules[{0}] PriceCeiling < PriceFloor."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Seen.Contains(Rule.CommodityTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("SimEco", "RuleDup", "Duplicate rule for commodity {0}."),
				FText::FromName(Rule.CommodityTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Rule.CommodityTag);
	}

	return Result;
}
#endif
