// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Process/SimEco_ProcessDef.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

USimEco_ProcessDef::USimEco_ProcessDef()
{
	// Defaults inline on members.
}

FName USimEco_ProcessDef::GetDataAssetType_Implementation() const
{
	return FName(TEXT("SimEco.Process"));
}

#if WITH_EDITOR
EDataValidationResult USimEco_ProcessDef::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (CycleSeconds <= 0.0)
	{
		Context.AddError(FText::FromString(TEXT("SimEco_ProcessDef: CycleSeconds must be > 0.")));
		Result = EDataValidationResult::Invalid;
	}

	if (Inputs.Num() == 0 && Outputs.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("SimEco_ProcessDef: a process must have at least one input or output.")));
		Result = EDataValidationResult::Invalid;
	}

	for (const FSimEco_CommodityAmount& In : Inputs)
	{
		if (!In.IsValidAmount())
		{
			Context.AddError(FText::FromString(TEXT("SimEco_ProcessDef: an input has an invalid commodity tag or non-positive quantity.")));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (const FSimEco_CommodityAmount& Out : Outputs)
	{
		if (!Out.IsValidAmount())
		{
			Context.AddError(FText::FromString(TEXT("SimEco_ProcessDef: an output has an invalid commodity tag or non-positive quantity.")));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
