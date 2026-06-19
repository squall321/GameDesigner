// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/DPDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "UObject/UObjectIterator.h"
#define LOCTEXT_NAMESPACE "DP_DataAsset"
#endif

FName UDP_DataAsset::GetDataAssetType_Implementation() const
{
	// One asset-manager type bucket per concrete subclass by default.
	return GetClass()->GetFName();
}

FPrimaryAssetId UDP_DataAsset::GetPrimaryAssetId() const
{
	// Default-class CDOs must not advertise a real primary asset id.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return FPrimaryAssetId();
	}
	return FPrimaryAssetId(GetDataAssetType(), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UDP_DataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!DataTag.IsValid())
	{
		Context.AddError(LOCTEXT("EmptyDataTag",
			"UDP_DataAsset has an empty DataTag. A unique, valid DataTag is required for registry indexing."));
		Result = EDataValidationResult::Invalid;
	}
	else
	{
		// Flag duplicates among other loaded DP data assets. The asset registry catches the
		// load-free case at runtime; this in-editor pass catches obvious collisions on save.
		for (TObjectIterator<UDP_DataAsset> It; It; ++It)
		{
			const UDP_DataAsset* Other = *It;
			if (Other == this || !IsValid(Other) || Other->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}
			if (Other->DataTag == DataTag)
			{
				Context.AddError(FText::Format(
					LOCTEXT("DuplicateDataTag", "DataTag '{0}' is also used by '{1}'. DataTags must be unique."),
					FText::FromString(DataTag.ToString()),
					FText::FromString(Other->GetPathName())));
				Result = EDataValidationResult::Invalid;
				break;
			}
		}
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
