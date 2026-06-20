// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Audio_DeveloperSettings.h"

UAudio_DeveloperSettings::UAudio_DeveloperSettings()
{
	// Section name shown in Project Settings (under Plugins). The defaults declared in the header are
	// the genre-neutral fallbacks; projects override them in DefaultGame.ini via this DeveloperSettings.
}

const UAudio_DeveloperSettings* UAudio_DeveloperSettings::Get()
{
	// GetDefault on a Config DeveloperSettings returns the CDO populated from the project's ini —
	// the canonical, always-valid source of these tunables at runtime and in the editor.
	return GetDefault<UAudio_DeveloperSettings>();
}

float UAudio_DeveloperSettings::ResolveCategoryDefaultVolume(const FGameplayTag& Category) const
{
	// Exact match first, then walk parents so a leaf category inherits an ancestor's configured volume.
	if (Category.IsValid())
	{
		if (const float* Exact = CategoryDefaultVolumes.Find(Category))
		{
			return FMath::Max(0.f, *Exact);
		}
		for (FGameplayTag Parent = Category.RequestDirectParent(); Parent.IsValid(); Parent = Parent.RequestDirectParent())
		{
			if (const float* Found = CategoryDefaultVolumes.Find(Parent))
			{
				return FMath::Max(0.f, *Found);
			}
		}
	}
	return FMath::Max(0.f, DefaultCategoryVolume);
}

int32 UAudio_DeveloperSettings::ResolveCategoryVoiceCap(const FGameplayTag& Category) const
{
	// A configured non-positive cap means "no category cap" -> fall back to the global MaxVoices.
	if (Category.IsValid())
	{
		auto ReadCap = [](const int32* Ptr) -> int32 { return (Ptr && *Ptr > 0) ? *Ptr : INDEX_NONE; };

		if (const int32* Exact = CategoryVoiceCaps.Find(Category))
		{
			const int32 Cap = ReadCap(Exact);
			if (Cap != INDEX_NONE)
			{
				return Cap;
			}
		}
		for (FGameplayTag Parent = Category.RequestDirectParent(); Parent.IsValid(); Parent = Parent.RequestDirectParent())
		{
			if (const int32* Found = CategoryVoiceCaps.Find(Parent))
			{
				const int32 Cap = ReadCap(Found);
				if (Cap != INDEX_NONE)
				{
					return Cap;
				}
			}
		}
	}
	// Defensive lower bound: MaxVoices is ClampMin=1 in the editor, but guard against a hand-edited
	// ini producing a non-positive value so the manager never gets a zero/negative budget.
	return (MaxVoices > 0) ? MaxVoices : 64;
}
