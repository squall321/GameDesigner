// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Cam_DeveloperSettings.h"
#include "Mode/Cam_CameraMode.h"
#include "Mode/Cam_StandardModes.h"
#include "Cam_NativeTags.h"
#include "Core/DPLog.h"

UCam_DeveloperSettings::UCam_DeveloperSettings()
{
	// Ship default wiring for the five built-in mode tags. These are CDO defaults; a project can
	// override any of them in DefaultGame.ini. Using hard class refs (TSoftClassPtr from a hard
	// UClass*) keeps the defaults valid even before the config is authored.
	auto AddDefault = [this](const FGameplayTag& Tag, UClass* Class)
	{
		FCam_ModeMapping Mapping;
		Mapping.ModeTag = Tag;
		Mapping.ModeClass = Class;
		ModeMappings.Add(Mapping);
	};

	AddDefault(Cam_NativeTags::Mode_ThirdPerson, UCam_ThirdPersonFollowMode::StaticClass());
	AddDefault(Cam_NativeTags::Mode_FirstPerson, UCam_FirstPersonMode::StaticClass());
	AddDefault(Cam_NativeTags::Mode_TopDown, UCam_TopDownMode::StaticClass());
	AddDefault(Cam_NativeTags::Mode_Orbit, UCam_OrbitMode::StaticClass());
	AddDefault(Cam_NativeTags::Mode_Fixed, UCam_FixedMode::StaticClass());

	// Default to a third-person follow camera so a freshly-added director "just works".
	DefaultModeTag = Cam_NativeTags::Mode_ThirdPerson;
}

const UCam_DeveloperSettings* UCam_DeveloperSettings::Get()
{
	return GetDefault<UCam_DeveloperSettings>();
}

TSubclassOf<UCam_CameraMode> UCam_DeveloperSettings::ResolveModeClass(FGameplayTag ModeTag) const
{
	if (!ModeTag.IsValid())
	{
		return nullptr;
	}
	for (const FCam_ModeMapping& Mapping : ModeMappings)
	{
		if (Mapping.ModeTag == ModeTag)
		{
			// Synchronous load is acceptable: mode classes are tiny and pushes are user-driven events.
			if (UClass* Loaded = Mapping.ModeClass.LoadSynchronous())
			{
				return Loaded;
			}
			UE_LOG(LogDP, Warning, TEXT("[Camera] Mode tag %s is mapped but its class failed to load."),
				*ModeTag.ToString());
			return nullptr;
		}
	}
	UE_LOG(LogDP, Warning, TEXT("[Camera] No mode class mapped for tag %s in Camera developer settings."),
		*ModeTag.ToString());
	return nullptr;
}
