// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Schedule/SimAg_ScheduleAsset.h"
#include "DesignPatternsSimAgentsTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SimAg_ScheduleAsset"

USimAg_ScheduleAsset::USimAg_ScheduleAsset()
{
}

FSimAg_ScheduleEntry USimAg_ScheduleAsset::ResolveActivityForHour(float HourOfDay) const
{
	if (Entries.Num() == 0)
	{
		return FSimAg_ScheduleEntry();
	}

	// Work on a sorted copy so authoring order doesn't matter and resolution is deterministic.
	TArray<FSimAg_ScheduleEntry> Sorted = Entries;
	Sorted.Sort(); // by StartHour (FSimAg_ScheduleEntry::operator<)

	// Pick the entry with the greatest StartHour <= HourOfDay.
	const FSimAg_ScheduleEntry* Best = nullptr;
	for (const FSimAg_ScheduleEntry& Entry : Sorted)
	{
		if (Entry.StartHour <= HourOfDay)
		{
			Best = &Entry;
		}
		else
		{
			break; // sorted ascending; no later entry can qualify
		}
	}

	// If HourOfDay precedes every entry's StartHour, the last entry of the day wraps past midnight.
	if (!Best)
	{
		Best = &Sorted.Last();
	}

	return *Best;
}

FName USimAg_ScheduleAsset::GetDataAssetType_Implementation() const
{
	// One shared bucket so all schedules group together in the asset manager regardless of subclass.
	return FName("SimAg_Schedule");
}

#if WITH_EDITOR
EDataValidationResult USimAg_ScheduleAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Entries.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("EmptySchedule", "Schedule has no entries; agents using it resolve to no activity."));
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FSimAg_ScheduleEntry& Entry = Entries[Index];
		if (Entry.StartHour < 0.f)
		{
			Context.AddError(FText::Format(
				LOCTEXT("NegativeHour", "Schedule entry {0} has a negative StartHour ({1})."),
				FText::AsNumber(Index), FText::AsNumber(Entry.StartHour)));
			Result = EDataValidationResult::Invalid;
		}
		if (!Entry.Activity.IsValid())
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("NoActivity", "Schedule entry {0} has no Activity tag."),
				FText::AsNumber(Index)));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
