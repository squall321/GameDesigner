// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Audio_MusicDirectorSettings.generated.h"

class UAudio_MusicEventMapDataAsset;
class USoundClass;

/**
 * Project settings (Project Settings -> Plugins -> DesignPatterns Audio) for the adaptive music
 * director.
 *
 * Holds the DEFAULT event map the director installs at startup plus engine-side tunables (sound
 * class, default crossfade fallback). All gameplay numbers live here or on data assets — never
 * hardcoded in the subsystem. When this CDO is unreachable (extremely early load) the director
 * uses the documented defensive fallbacks declared next to each accessor in the .cpp.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DesignPatterns Audio - Music Director"))
class DESIGNPATTERNSAUDIO_API UAudio_MusicDirectorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAudio_MusicDirectorSettings();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	/**
	 * Event map the director installs automatically on Initialize. May be empty: a game can instead
	 * install a map at runtime via UAudio_MusicDirectorSubsystem::InstallEventMap. Soft so the asset
	 * is only loaded when actually used.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Music",
		meta = (AllowedClasses = "/Script/DesignPatternsAudio.Audio_MusicEventMapDataAsset"))
	TSoftObjectPtr<UAudio_MusicEventMapDataAsset> DefaultEventMap;

	/**
	 * Optional sound class routed to all director-spawned music voices, so a game can attach the
	 * music bus / mix to it. Soft; when unset the audio components use their assets' own class.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Music")
	TSoftObjectPtr<USoundClass> MusicSoundClass;

	/**
	 * Defensive fallback crossfade (seconds) used only when a target state's own CrossfadeSeconds is
	 * negative/unset AND no map override applies. States normally drive their own crossfade.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Music",
		meta = (ClampMin = "0.0", Units = "s"))
	float FallbackCrossfadeSeconds = 2.0f;

	/**
	 * How quickly (per second, normalized units) SetIntensity eases toward its target so abrupt
	 * gameplay intensity jumps don't pop the layer mix. 0 = snap instantly.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Music",
		meta = (ClampMin = "0.0"))
	float IntensityInterpSpeed = 3.0f;

	/** Master scalar applied to ALL music voices (debug/master balance). 1.0 = unity. */
	UPROPERTY(EditAnywhere, Config, Category = "Music",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float MasterMusicVolume = 1.0f;

	/** Null-safe CDO accessor with the documented hardcoded fallbacks baked in. */
	static const UAudio_MusicDirectorSettings& GetChecked();
};
