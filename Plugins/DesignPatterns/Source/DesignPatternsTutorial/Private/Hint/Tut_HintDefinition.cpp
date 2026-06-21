// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hint/Tut_HintDefinition.h"
#include "Tutorial/Tut_Condition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Tut_HintDefinition"

FName UTut_HintDefinition::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Tut_HintDefinition"));
}

#if WITH_EDITOR
EDataValidationResult UTut_HintDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Text.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoText", "Hint definition has empty Text; it will surface a blank toast."));
	}

	if (Condition == nullptr)
	{
		Context.AddWarning(LOCTEXT("NoCondition", "Hint definition has no Condition; it is never eligible to surface automatically."));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
