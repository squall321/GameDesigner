// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Commodity/SimEco_CommodityDef.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

USimEco_CommodityDef::USimEco_CommodityDef()
{
	// Defaults inline on members.
}

FName USimEco_CommodityDef::GetDataAssetType_Implementation() const
{
	return FName(TEXT("SimEco.Commodity"));
}

double USimEco_CommodityDef::Quantize(double RawQuantity) const
{
	if (RawQuantity <= 0.0)
	{
		return 0.0;
	}
	if (bDivisible)
	{
		return RawQuantity;
	}
	// Indivisible: whole units only, never negative.
	return FMath::FloorToDouble(RawQuantity);
}

#if WITH_EDITOR
EDataValidationResult USimEco_CommodityDef::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (BaseValue < 0.0)
	{
		Context.AddError(FText::FromString(TEXT("SimEco_CommodityDef: BaseValue must be >= 0.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
