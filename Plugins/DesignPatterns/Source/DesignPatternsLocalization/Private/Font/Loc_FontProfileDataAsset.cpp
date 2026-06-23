// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Font/Loc_FontProfileDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FLoc_FontRoleOverride* ULoc_FontProfileDataAsset::FindRoleOverride(FGameplayTag Role) const
{
	if (!Role.IsValid())
	{
		return nullptr;
	}

	const FLoc_FontRoleOverride* Best = nullptr;
	int32 BestDepth = -1;

	const FString RoleString = Role.ToString();

	for (const FLoc_FontRoleOverride& Override : RoleOverrides)
	{
		if (!Override.Role.IsValid())
		{
			continue;
		}

		// Exact or ancestor match (hierarchy-aware); prefer the deepest (most specific) matching role.
		const bool bMatches = (Role == Override.Role) || Role.MatchesTag(Override.Role);
		if (!bMatches)
		{
			continue;
		}

		const FString OverrideString = Override.Role.ToString();
		int32 Depth = 0;
		for (TCHAR Ch : OverrideString)
		{
			if (Ch == TEXT('.'))
			{
				++Depth;
			}
		}

		if (Depth > BestDepth)
		{
			BestDepth = Depth;
			Best = &Override;
		}
	}

	return Best;
}

FName ULoc_FontProfileDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Loc_FontProfile"));
}

#if WITH_EDITOR
EDataValidationResult ULoc_FontProfileDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Culture.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("Loc_FontProfile: Culture is empty; this profile can never be selected.")));
		Result = EDataValidationResult::Invalid;
	}

	if (DefaultFontFace.IsNull())
	{
		// Not fatal: an unset default means the engine default font is used (documented inert fallback),
		// but it is almost always a mistake on a culture profile, so warn.
		Context.AddWarning(FText::FromString(
			TEXT("Loc_FontProfile: DefaultFontFace is unset; the engine default font will be used for unoverridden roles.")));
	}

	for (const FLoc_FontRoleOverride& Override : RoleOverrides)
	{
		if (!Override.Role.IsValid())
		{
			Context.AddError(FText::FromString(TEXT("Loc_FontProfile: a role override has an invalid (empty) Role tag.")));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
