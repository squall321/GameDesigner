// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/AI_WaveDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

int32 UAI_WaveDataAsset::GetPlannedBudgetCost() const
{
	int32 Total = 0;
	for (const FAI_SpawnEntry& Entry : Entries)
	{
		// Count and BudgetCost are clamped >= 1 by their UPROPERTY meta; still guard defensively.
		Total += FMath::Max(0, Entry.Count) * FMath::Max(0, Entry.BudgetCost);
	}
	return Total;
}

int32 UAI_WaveDataAsset::GetPlannedCount() const
{
	int32 Total = 0;
	for (const FAI_SpawnEntry& Entry : Entries)
	{
		Total += FMath::Max(0, Entry.Count);
	}
	return Total;
}

FGameplayTag UAI_WaveDataAsset::GetEffectiveWaveTag() const
{
	return WaveTag.IsValid() ? WaveTag : DataTag;
}

FName UAI_WaveDataAsset::GetDataAssetType_Implementation() const
{
	// All wave assets share one asset-manager bucket so a project can scan/cook them together.
	return FName(TEXT("AI_Wave"));
}

#if WITH_EDITOR
EDataValidationResult UAI_WaveDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Entries.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("AI Wave asset has no spawn entries.")));
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FAI_SpawnEntry& Entry = Entries[Index];
		const bool bHasFactory = Entry.FactoryIdentityTag.IsValid();
		const bool bHasClass = !Entry.ActorClassOverride.IsNull();
		if (!bHasFactory && !bHasClass)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("AI Wave entry %d has neither a FactoryIdentityTag nor an ActorClassOverride."), Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (!Entry.SpawnRegionTag.IsValid())
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("AI Wave entry %d has no SpawnRegionTag; the director will use its first fallback point."), Index)));
		}
	}

	return Result;
}
#endif
