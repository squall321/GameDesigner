// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tutorial/Tut_TutorialDefinition.h"
#include "Tutorial/Tut_Condition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Tut_TutorialDefinition"

FName UTut_TutorialDefinition::GetDataAssetType_Implementation() const
{
	// Collapse every tutorial definition into one shared asset-manager type so a project can scan/cook all
	// tutorials as a group regardless of any per-game subclass.
	return FName(TEXT("Tut_TutorialDefinition"));
}

#if WITH_EDITOR
EDataValidationResult UTut_TutorialDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Steps.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("NoSteps", "Tutorial definition has no steps; starting it completes immediately."));
		Result = CombineDataValidationResults(Result, EDataValidationResult::Valid);
	}

	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FTut_TutorialStep& Step = Steps[Index];

		// A step that neither shows text nor has a way to complete (no completion condition) and is not the
		// only step would stall the sequence; flag it so designers add an instruction or completion.
		if (Step.Instruction.IsEmpty() && Step.Completion == nullptr)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("EmptyStepFmt", "Step {0} has no instruction and no completion condition; it will be skipped on entry."),
				FText::AsNumber(Index)));
		}

		if (Step.bGateInput && !Step.InputModeTag.IsValid())
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("GateNoModeFmt", "Step {0} gates input but specifies no InputModeTag; the runner will use the settings default."),
				FText::AsNumber(Index)));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
