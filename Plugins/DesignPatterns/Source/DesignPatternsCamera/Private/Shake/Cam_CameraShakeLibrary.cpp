// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shake/Cam_CameraShakeLibrary.h"
#include "Camera/CameraShakeBase.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Cam_CameraShakeLibrary"

UCam_CameraShakeLibrary::UCam_CameraShakeLibrary()
{
	// No defaults assigned to Entries: the library is meaningless until a project authors rows.
}

void UCam_CameraShakeLibrary::EnsureIndex() const
{
	if (bIndexBuilt)
	{
		return;
	}

	IndexByTag.Reset();
	IndexByTag.Reserve(Entries.Num());
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FCam_ShakeEntry& Entry = Entries[Index];
		if (!Entry.ShakeTag.IsValid())
		{
			continue;
		}
		// First-wins on duplicate tags; duplicates are flagged in IsDataValid so authors notice.
		IndexByTag.FindOrAdd(Entry.ShakeTag, Index);
	}
	bIndexBuilt = true;
}

const FCam_ShakeEntry* UCam_CameraShakeLibrary::FindEntryExact(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}
	EnsureIndex();
	if (const int32* FoundIndex = IndexByTag.Find(Tag))
	{
		return Entries.IsValidIndex(*FoundIndex) ? &Entries[*FoundIndex] : nullptr;
	}
	return nullptr;
}

const FCam_ShakeEntry* UCam_CameraShakeLibrary::FindEntryForChannel(const FGameplayTag& Channel) const
{
	if (!Channel.IsValid())
	{
		return nullptr;
	}

	// Exact match short-circuits.
	if (const FCam_ShakeEntry* Exact = FindEntryExact(Channel))
	{
		return Exact;
	}

	// Otherwise pick the deepest (most specific) entry whose ShakeTag is an ancestor of Channel.
	// "Ancestor" = Channel.MatchesTag(EntryTag) is true while EntryTag != Channel. Depth is the
	// number of dot-separated components; deeper ancestor is the more specific override.
	const FCam_ShakeEntry* Best = nullptr;
	int32 BestDepth = -1;
	for (const FCam_ShakeEntry& Entry : Entries)
	{
		if (!Entry.ShakeTag.IsValid() || Entry.ShakeTag == Channel)
		{
			continue;
		}
		if (Channel.MatchesTag(Entry.ShakeTag))
		{
			const int32 Depth = Entry.ShakeTag.ToString().Len(); // longer string == deeper/more specific
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Entry;
			}
		}
	}
	return Best;
}

FName UCam_CameraShakeLibrary::GetDataAssetType_Implementation() const
{
	// Collapse all shake libraries into one asset-manager bucket so the asset manager can address
	// them uniformly regardless of subclass.
	return TEXT("Cam_CameraShakeLibrary");
}

#if WITH_EDITOR
void UCam_CameraShakeLibrary::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	InvalidateIndex();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

EDataValidationResult UCam_CameraShakeLibrary::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FCam_ShakeEntry& Entry = Entries[Index];

		if (!Entry.ShakeTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("ShakeEntryNoTag", "Shake entry [{0}] has no ShakeTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		SeenTags.Add(Entry.ShakeTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("ShakeEntryDuplicate", "Duplicate ShakeTag '{0}' at entry [{1}]; only the first is used."),
				FText::FromString(Entry.ShakeTag.ToString()), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.ShakeClass == nullptr)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("ShakeEntryNullClass", "Shake entry '{0}' has a null ShakeClass; it will be a no-op."),
				FText::FromString(Entry.ShakeTag.ToString())));
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
