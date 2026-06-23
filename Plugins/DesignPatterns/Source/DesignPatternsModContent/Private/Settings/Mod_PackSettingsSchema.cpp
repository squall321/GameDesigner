// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Mod_PackSettingsSchema.h"
#include "DesignPatternsModContentModule.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Mod_PackSettingsSchema"

FName UMod_PackSettingsSchema::GetDataAssetType_Implementation() const
{
	// All pack settings schemas share one asset-manager type so tooling can enumerate them in one query.
	static const FName SchemaType(TEXT("Mod_PackSettingsSchema"));
	return SchemaType;
}

#if WITH_EDITOR
EDataValidationResult UMod_PackSettingsSchema::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// The owning DataTag must live under DP.Mod.Pack so it pairs with a real pack id.
	if (DataTag.IsValid() && !DataTag.MatchesTag(ModTags::Pack))
	{
		Context.AddError(FText::Format(
			LOCTEXT("OwnerNotUnderRoot", "Settings schema owner '{0}' must be a child of DP.Mod.Pack."),
			FText::FromName(DataTag.GetTagName())));
		Result = EDataValidationResult::Invalid;
	}

	TSet<FGameplayTag> Seen;
	for (const FMod_PackSettingField& Field : Fields)
	{
		if (!Field.FieldId.IsValid())
		{
			Context.AddError(LOCTEXT("EmptyFieldId", "A settings field has an empty field id."));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (Seen.Contains(Field.FieldId))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateFieldId", "Settings field '{0}' is declared more than once."),
				FText::FromName(Field.FieldId.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Field.FieldId);

		// A numeric field that declares an inverted clamp (Max < Min, Max != 0) is a likely authoring error.
		const bool bNumeric = Field.Kind == EMod_SettingKind::Int || Field.Kind == EMod_SettingKind::Float;
		if (bNumeric && Field.ClampMax != 0.f && Field.ClampMax < Field.ClampMin)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("InvertedClamp", "Settings field '{0}' has ClampMax < ClampMin; clamp will be ignored."),
				FText::FromName(Field.FieldId.GetTagName())));
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
