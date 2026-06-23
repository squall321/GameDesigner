// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Anim/DPUIEasingDataAsset.h"
#include "Curves/CurveFloat.h"

UCurveFloat* UDP_UIEasingDataAsset::ResolveCurve(FGameplayTag EaseTag) const
{
	if (const TObjectPtr<UCurveFloat>* Found = Curves.Find(EaseTag))
	{
		return *Found;
	}
	return nullptr;
}

FName UDP_UIEasingDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("DP_UIEasing"));
}
