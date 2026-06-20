// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/WorldHub_FlagSetDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "WorldHub_FlagSet"
#endif

UWorldHub_FlagSetDataAsset::UWorldHub_FlagSetDataAsset()
{
}

const FWorldHub_FlagDefinition* UWorldHub_FlagSetDataAsset::FindDefinition(const FGameplayTag& Key) const
{
	if (!Key.IsValid())
	{
		return nullptr;
	}

	// Last definition with a matching key wins, mirroring the runtime "last duplicate wins" rule.
	const FWorldHub_FlagDefinition* Found = nullptr;
	for (const FWorldHub_FlagDefinition& Definition : Definitions)
	{
		if (Definition.Key == Key)
		{
			Found = &Definition;
		}
	}
	return Found;
}

bool UWorldHub_FlagSetDataAsset::FindDefinitionByKey(const FGameplayTag& Key, FWorldHub_FlagDefinition& OutDefinition) const
{
	if (const FWorldHub_FlagDefinition* Found = FindDefinition(Key))
	{
		OutDefinition = *Found;
		return true;
	}
	OutDefinition = FWorldHub_FlagDefinition();
	return false;
}

void UWorldHub_FlagSetDataAsset::GetAllKeys(TArray<FGameplayTag>& OutKeys) const
{
	OutKeys.Reset();
	OutKeys.Reserve(Definitions.Num());
	for (const FWorldHub_FlagDefinition& Definition : Definitions)
	{
		OutKeys.Add(Definition.Key);
	}
}

FName UWorldHub_FlagSetDataAsset::GetDataAssetType_Implementation() const
{
	// All flag sets share one asset-manager bucket so the hub can scan them as a family.
	return FName(TEXT("WorldHub_FlagSet"));
}

#if WITH_EDITOR
EDataValidationResult UWorldHub_FlagSetDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenKeys;
	SeenKeys.Reserve(Definitions.Num());

	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FWorldHub_FlagDefinition& Definition = Definitions[Index];

		if (!Definition.Key.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("EmptyFlagKey", "Flag definition at index {0} has an empty Key. Every flag needs a valid tag key."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		SeenKeys.Add(Definition.Key, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateFlagKey", "Flag key '{0}' is defined more than once in this set. Keys must be unique."),
				FText::FromString(Definition.Key.ToString())));
			Result = EDataValidationResult::Invalid;
		}

		if (Definition.ValueType == EWorldHub_FlagValueType::Counter && Definition.CounterMin > Definition.CounterMax)
		{
			Context.AddError(FText::Format(
				LOCTEXT("BadCounterBounds", "Counter flag '{0}' has CounterMin ({1}) greater than CounterMax ({2})."),
				FText::FromString(Definition.Key.ToString()),
				FText::AsNumber(Definition.CounterMin),
				FText::AsNumber(Definition.CounterMax)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
