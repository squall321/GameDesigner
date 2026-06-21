// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tutorial/Tut_TutorialViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UTut_TutorialViewModel's observable fields by name. */
	struct FTut_TutorialViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UTut_TutorialViewModel::EField::Num];

		static FFieldId MakeId(UTut_TutorialViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UTut_TutorialViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FTut_TutorialViewModelDescriptor::FieldNames[(int32)UTut_TutorialViewModel::EField::Num] =
	{
		FName(TEXT("bActive")),
		FName(TEXT("Instruction")),
		FName(TEXT("HighlightTarget")),
		FName(TEXT("StepIndex")),
		FName(TEXT("StepCount")),
		FName(TEXT("TutorialTag")),
	};

	static const FTut_TutorialViewModelDescriptor GTut_TutorialViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UTut_TutorialViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GTut_TutorialViewModelDescriptor;
}

UE::FieldNotification::FFieldId UTut_TutorialViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FTut_TutorialViewModelDescriptor::MakeId(Field);
}

void UTut_TutorialViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UTut_TutorialViewModel::SetActiveStep(
	const FGameplayTag& InTutorialTag,
	int32 InStepIndex,
	int32 InStepCount,
	const FText& InInstruction,
	const FGameplayTag& InHighlightTarget)
{
	if (!bActive)
	{
		bActive = true;
		BroadcastField(EField::bActive);
	}

	if (TutorialTag != InTutorialTag)
	{
		TutorialTag = InTutorialTag;
		BroadcastField(EField::TutorialTag);
	}

	if (StepIndex != InStepIndex)
	{
		StepIndex = InStepIndex;
		BroadcastField(EField::StepIndex);
	}

	if (StepCount != InStepCount)
	{
		StepCount = InStepCount;
		BroadcastField(EField::StepCount);
	}

	// FText has no operator!= usable for SetProperty; compare via EqualTo (handles localized identity).
	if (!Instruction.EqualTo(InInstruction))
	{
		Instruction = InInstruction;
		BroadcastField(EField::Instruction);
	}

	if (HighlightTarget != InHighlightTarget)
	{
		HighlightTarget = InHighlightTarget;
		BroadcastField(EField::HighlightTarget);
	}
}

void UTut_TutorialViewModel::ClearActive()
{
	if (bActive)
	{
		bActive = false;
		BroadcastField(EField::bActive);
	}

	if (!Instruction.IsEmpty())
	{
		Instruction = FText::GetEmpty();
		BroadcastField(EField::Instruction);
	}

	if (HighlightTarget.IsValid())
	{
		HighlightTarget = FGameplayTag();
		BroadcastField(EField::HighlightTarget);
	}

	if (StepIndex != INDEX_NONE)
	{
		StepIndex = INDEX_NONE;
		BroadcastField(EField::StepIndex);
	}

	if (StepCount != 0)
	{
		StepCount = 0;
		BroadcastField(EField::StepCount);
	}

	if (TutorialTag.IsValid())
	{
		TutorialTag = FGameplayTag();
		BroadcastField(EField::TutorialTag);
	}
}
