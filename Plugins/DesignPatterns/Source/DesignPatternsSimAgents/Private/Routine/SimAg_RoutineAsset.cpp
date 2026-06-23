// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Routine/SimAg_RoutineAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SimAg_RoutineAsset"

namespace
{
	/** Hour-of-day in [0, HoursPerDay) from a fractional-day time. */
	float HourOfDayFromDays(double FractionalDays, int32 HoursPerDay)
	{
		const int32 SafeHours = FMath::Max(1, HoursPerDay);
		const double Frac = FractionalDays - FMath::FloorToDouble(FractionalDays);
		return static_cast<float>(Frac * SafeHours);
	}
}

USimAg_RoutineAsset::USimAg_RoutineAsset()
{
}

FSimAg_RoutineStep USimAg_RoutineAsset::ResolveStepForTime(double FractionalDays, int32 HoursPerDay) const
{
	if (Steps.Num() == 0)
	{
		return FSimAg_RoutineStep();
	}
	const float HourOfDay = HourOfDayFromDays(FractionalDays, HoursPerDay);

	TArray<FSimAg_RoutineStep> Sorted = Steps;
	Sorted.Sort(); // by StartHour

	const FSimAg_RoutineStep* Best = nullptr;
	for (const FSimAg_RoutineStep& Step : Sorted)
	{
		if (Step.StartHour <= HourOfDay)
		{
			Best = &Step;
		}
		else
		{
			break;
		}
	}
	if (!Best)
	{
		Best = &Sorted.Last(); // wraps past midnight
	}
	return *Best;
}

bool USimAg_RoutineAsset::ResolveUpcomingStep(double FractionalDays, int32 HoursPerDay, FSimAg_RoutineStep& OutStep, float& OutHoursUntil) const
{
	if (Steps.Num() == 0)
	{
		return false;
	}
	const int32 SafeHours = FMath::Max(1, HoursPerDay);
	const float HourOfDay = HourOfDayFromDays(FractionalDays, SafeHours);

	TArray<FSimAg_RoutineStep> Sorted = Steps;
	Sorted.Sort();

	// First step whose StartHour is strictly after the current hour-of-day.
	for (const FSimAg_RoutineStep& Step : Sorted)
	{
		if (Step.StartHour > HourOfDay)
		{
			OutStep = Step;
			OutHoursUntil = Step.StartHour - HourOfDay;
			return true;
		}
	}
	// None later today: the next step is the first one tomorrow.
	OutStep = Sorted[0];
	OutHoursUntil = (static_cast<float>(SafeHours) - HourOfDay) + Sorted[0].StartHour;
	return true;
}

FName USimAg_RoutineAsset::GetDataAssetType_Implementation() const
{
	return FName("SimAg_Routine");
}

#if WITH_EDITOR
EDataValidationResult USimAg_RoutineAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Steps.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("EmptyRoutine", "Routine has no steps; agents using it resolve to no activity."));
	}
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FSimAg_RoutineStep& Step = Steps[Index];
		if (Step.StartHour < 0.f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("NegativeHour", "Routine step {0} has a negative StartHour ({1})."),
				FText::AsNumber(Index), FText::AsNumber(Step.StartHour)));
			Result = EDataValidationResult::Invalid;
		}
		if (!Step.Activity.IsValid())
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("NoActivity", "Routine step {0} has no Activity tag."), FText::AsNumber(Index)));
		}
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
