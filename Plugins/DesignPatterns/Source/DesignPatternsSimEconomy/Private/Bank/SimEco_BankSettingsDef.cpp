// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Bank/SimEco_BankSettingsDef.h"

USimEco_BankSettingsDef::USimEco_BankSettingsDef()
{
}

FName USimEco_BankSettingsDef::GetDataAssetType_Implementation() const
{
	return FName(TEXT("SimEco_BankSettings"));
}

int64 USimEco_BankSettingsDef::ComputeMaxLoan(float Reputation) const
{
	double Mult = 1.0;
	if (ReputationToLoanMultiplierCurve)
	{
		Mult = FMath::Max(0.0f, ReputationToLoanMultiplierCurve->GetFloatValue(Reputation));
	}
	const double Max = (double)FMath::Max((int64)0, BaseMaxLoan) * Mult;
	return (int64)FMath::Max(0.0, FMath::RoundToDouble(Max));
}
