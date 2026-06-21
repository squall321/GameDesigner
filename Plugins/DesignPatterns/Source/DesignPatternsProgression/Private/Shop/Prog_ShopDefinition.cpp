// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shop/Prog_ShopDefinition.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UProg_ShopDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenItems;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FProg_ShopEntry& Entry = Entries[Index];

		if (!Entry.ItemTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Prog_Shop", "EmptyItemTag", "Shop entry {0} has no ItemTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenItems.Contains(Entry.ItemTag))
		{
			// Duplicates are allowed (e.g. a buy-1 and a buy-10 bundle of the same item), but usually a
			// mistake; surface as a warning, not an error.
			Context.AddWarning(FText::Format(
				NSLOCTEXT("Prog_Shop", "DuplicateItem", "Shop entry {0} duplicates item {1}."),
				FText::AsNumber(Index), FText::FromString(Entry.ItemTag.ToString())));
		}
		SeenItems.Add(Entry.ItemTag);

		if (Entry.Price < 0)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Prog_Shop", "NegativePrice", "Shop entry {0} has a negative price."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.Price > 0 && !Entry.PriceCurrency.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Prog_Shop", "PricedNoCurrency", "Shop entry {0} is priced but has no PriceCurrency."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.GrantCount < 1)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Prog_Shop", "BadGrantCount", "Shop entry {0} has a GrantCount below 1."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR
