// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/Lvl_PlacementRuleSet.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

ULvl_PlacementRuleSet::ULvl_PlacementRuleSet()
{
	// All gameplay tunables default on their UPROPERTY declarations (data-driven, no magic numbers in
	// code). The constructor only seeds a single sensible class-choice slot so a freshly-created asset
	// is editor-valid-shaped (still empty of identity until a designer fills it in).
}

float ULvl_PlacementRuleSet::GetTotalChoiceWeight() const
{
	float Total = 0.f;
	for (const FLvl_PlacementClassChoice& Choice : ClassChoices)
	{
		if (Choice.IsUsable())
		{
			Total += FMath::Max(0.f, Choice.Weight);
		}
	}
	return Total;
}

bool ULvl_PlacementRuleSet::HasUsableClass() const
{
	for (const FLvl_PlacementClassChoice& Choice : ClassChoices)
	{
		if (Choice.IsUsable())
		{
			return true;
		}
	}
	return false;
}

const FLvl_PlacementClassChoice* ULvl_PlacementRuleSet::PickClassChoice(FRandomStream& Stream) const
{
	if (ClassChoices.Num() == 0)
	{
		return nullptr;
	}

	const float TotalWeight = GetTotalChoiceWeight();

	// Weighted selection when weights are meaningful.
	if (TotalWeight > 0.f)
	{
		const float Roll = Stream.FRandRange(0.f, TotalWeight);
		float Running = 0.f;
		for (const FLvl_PlacementClassChoice& Choice : ClassChoices)
		{
			if (!Choice.IsUsable())
			{
				continue;
			}
			Running += FMath::Max(0.f, Choice.Weight);
			if (Roll <= Running)
			{
				return &Choice;
			}
		}
	}

	// Degenerate (all-zero weight) but at least one usable choice: pick uniformly among usable ones.
	TArray<const FLvl_PlacementClassChoice*, TInlineAllocator<8>> Usable;
	for (const FLvl_PlacementClassChoice& Choice : ClassChoices)
	{
		if (Choice.IsUsable())
		{
			Usable.Add(&Choice);
		}
	}
	if (Usable.Num() == 0)
	{
		return nullptr;
	}
	const int32 Index = Stream.RandRange(0, Usable.Num() - 1);
	return Usable[Index];
}

void ULvl_PlacementRuleSet::GetClampedScaleRange(float& OutMin, float& OutMax) const
{
	OutMin = FMath::Max(0.01f, MinScale);
	OutMax = FMath::Max(OutMin, MaxScale);
}

#if WITH_EDITOR
EDataValidationResult ULvl_PlacementRuleSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!HasUsableClass())
	{
		Context.AddError(FText::FromString(TEXT(
			"Lvl_PlacementRuleSet has no usable class choice (each ClassChoice needs a valid ActorClassTag).")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < ClassChoices.Num(); ++Index)
	{
		if (!ClassChoices[Index].ActorClassTag.IsValid())
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("ClassChoice[%d] has no ActorClassTag and will be skipped at placement time."), Index)));
		}
	}

	if (MaxScale < MinScale)
	{
		Context.AddWarning(FText::FromString(TEXT(
			"MaxScale is less than MinScale; it will be clamped up to MinScale at placement time.")));
	}

	if (Source == ELvl_PlacementSource::BoxVolume && BoxExtent.X <= 0.0 && BoxExtent.Y <= 0.0)
	{
		Context.AddWarning(FText::FromString(TEXT(
			"BoxVolume source with zero XY extent will produce no candidates.")));
	}

	return Result;
}
#endif
