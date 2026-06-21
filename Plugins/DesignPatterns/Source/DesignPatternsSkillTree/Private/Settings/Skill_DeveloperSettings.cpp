// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Skill_DeveloperSettings.h"

USkill_DeveloperSettings::USkill_DeveloperSettings()
{
	// Section name shown under Project Settings → Plugins.
	CategoryName = FName("Plugins");
}

const USkill_DeveloperSettings* USkill_DeveloperSettings::Get()
{
	// GetDefault never returns null for a UDeveloperSettings CDO in a configured build.
	return GetDefault<USkill_DeveloperSettings>();
}

FString USkill_DeveloperSettings::GetSaveSlotPrefixSafe()
{
	// Documented defensive fallback: if the CDO is somehow unavailable, use the same default the header
	// declares so the save area never produces an empty slot prefix.
	if (const USkill_DeveloperSettings* Settings = Get())
	{
		if (!Settings->SaveSlotPrefix.IsEmpty())
		{
			return Settings->SaveSlotPrefix;
		}
	}
	return TEXT("SkillTree_");
}
