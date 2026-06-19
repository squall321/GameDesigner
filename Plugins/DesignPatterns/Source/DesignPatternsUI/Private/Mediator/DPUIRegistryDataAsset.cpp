// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mediator/DPUIRegistryDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FDP_ScreenDef* UDP_UIRegistryDataAsset::FindScreen(const FGameplayTag& ScreenTag) const
{
	if (!ScreenTag.IsValid())
	{
		return nullptr;
	}
	return Screens.FindByPredicate([&ScreenTag](const FDP_ScreenDef& Def)
	{
		return Def.ScreenTag == ScreenTag;
	});
}

bool UDP_UIRegistryDataAsset::GetScreenDef(FGameplayTag ScreenTag, FDP_ScreenDef& OutDef) const
{
	if (const FDP_ScreenDef* Found = FindScreen(ScreenTag))
	{
		OutDef = *Found;
		return true;
	}
	return false;
}

FPrimaryAssetId UDP_UIRegistryDataAsset::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("DP_UIRegistry"), GetFName());
}

#if WITH_EDITOR

#if UE_VERSION_OLDER_THAN(5, 4, 0)
EDataValidationResult UDP_UIRegistryDataAsset::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = Super::IsDataValid(ValidationErrors);
	auto AddError = [&ValidationErrors, &Result](const FString& Msg)
	{
		ValidationErrors.Add(FText::FromString(Msg));
		Result = EDataValidationResult::Invalid;
	};
	auto AddWarning = [&ValidationErrors](const FString& Msg)
	{
		ValidationErrors.Add(FText::FromString(Msg));
	};
#else
EDataValidationResult UDP_UIRegistryDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto AddError = [&Context, &Result](const FString& Msg)
	{
		Context.AddError(FText::FromString(Msg));
		Result = EDataValidationResult::Invalid;
	};
	auto AddWarning = [&Context](const FString& Msg)
	{
		Context.AddWarning(FText::FromString(Msg));
	};
#endif

	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Screens.Num(); ++Index)
	{
		const FDP_ScreenDef& Def = Screens[Index];

		if (!Def.ScreenTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Screen entry %d has an invalid ScreenTag."), Index));
		}
		else if (SeenTags.Contains(Def.ScreenTag))
		{
			AddError(FString::Printf(TEXT("Duplicate ScreenTag '%s' at entry %d."),
				*Def.ScreenTag.ToString(), Index));
		}
		else
		{
			SeenTags.Add(Def.ScreenTag);
		}

		if (Def.WidgetClass.IsNull())
		{
			AddWarning(FString::Printf(TEXT("Screen '%s' (entry %d) has no WidgetClass assigned."),
				*Def.ScreenTag.ToString(), Index));
		}

		if (!Def.LayerTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Screen '%s' (entry %d) has an invalid LayerTag."),
				*Def.ScreenTag.ToString(), Index));
		}
	}

	return Result;
}
#endif // WITH_EDITOR
