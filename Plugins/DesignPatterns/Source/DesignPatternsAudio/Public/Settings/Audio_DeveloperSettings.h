// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DeveloperSettings.h"
#include "Audio_DeveloperSettings.generated.h"

class UAudio_SoundBankDataAsset;
class UAudio_MixProfileDataAsset;

/**
 * Project-wide configuration for the DesignPatternsAudio module. Appears under
 * Project Settings -> Plugins -> Design Patterns Audio. Editing here requires no code.
 *
 * These are the genre-neutral tunables the sound manager and mix controller fall back to: the
 * default sound banks/mix profiles to load at startup, the master volume, per-category default
 * volume multipliers and the global voice budget. All values are exposed via
 * UPROPERTY(EditAnywhere, Config); there are no hardcoded magic gameplay numbers in code. The
 * sound manager reads the CDO via Get() and uses these as defensive fallbacks documented inline.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Audio"))
class DESIGNPATTERNSAUDIO_API UAudio_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAudio_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	/**
	 * Sound banks the sound manager loads at GameInstance start. Soft so the banks (and their soft
	 * sound refs) only cost what is actually referenced. A project may also push banks at runtime.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Banks")
	TArray<TSoftObjectPtr<UAudio_SoundBankDataAsset>> DefaultSoundBanks;

	/**
	 * Mix profiles the mix controller pre-resolves at start so a runtime push-by-tag does not stall
	 * on a synchronous load. Optional; a project can rely on the data registry / push-by-asset instead.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mix")
	TArray<TSoftObjectPtr<UAudio_MixProfileDataAsset>> DefaultMixProfiles;

	/**
	 * Master linear volume multiplier applied on top of every category/per-call volume. 1.0 = no
	 * change. This is the single global trim the sound manager multiplies into every voice.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float MasterVolume = 1.f;

	/**
	 * Default linear volume multiplier for any category that has no explicit override below or no
	 * runtime SetCategoryVolume. 1.0 = unattenuated.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Volume", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float DefaultCategoryVolume = 1.f;

	/**
	 * Per-category starting volume multipliers (child of DP.Audio.Category -> linear multiplier).
	 * Seeds the sound manager's runtime category-volume table; runtime SetCategoryVolume overrides
	 * these. Categories absent here start at DefaultCategoryVolume.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Volume", meta = (ForceInlineRow, Categories = "DP.Audio.Category"))
	TMap<FGameplayTag, float> CategoryDefaultVolumes;

	/**
	 * Global cap on simultaneously-tracked active voices across all categories. When exceeded the
	 * sound manager virtualizes (oldest-steal) to stay within budget. The defensive lower bound the
	 * manager uses if this is somehow non-positive is documented at the call site.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Voices", meta = (ClampMin = "1", UIMin = "8", UIMax = "256"))
	int32 MaxVoices = 64;

	/**
	 * Per-category voice budget that overrides MaxVoices for specific categories (child of
	 * DP.Audio.Category -> cap). Lets e.g. footsteps be tightly limited while music is uncapped.
	 * A non-positive value means "no category cap" (only MaxVoices applies).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Voices", meta = (ForceInlineRow, Categories = "DP.Audio.Category"))
	TMap<FGameplayTag, int32> CategoryVoiceCaps;

	/**
	 * Linear volume a ducked category drops to while a higher-priority duck rule is active
	 * (0 = full silence, 1 = no ducking). Used as the fallback duck depth when a mix profile's duck
	 * rule does not specify its own.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mix", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float DefaultDuckVolume = 0.4f;

	/** Convenience accessor (never null in a running game; the CDO is populated from the project ini). */
	static const UAudio_DeveloperSettings* Get();

	/**
	 * Resolve the effective starting volume for a category from CategoryDefaultVolumes (walking the
	 * tag hierarchy to the nearest configured ancestor), falling back to DefaultCategoryVolume.
	 */
	float ResolveCategoryDefaultVolume(const FGameplayTag& Category) const;

	/**
	 * Resolve the effective voice cap for a category from CategoryVoiceCaps (walking the tag
	 * hierarchy), falling back to MaxVoices. Always returns a positive number.
	 */
	int32 ResolveCategoryVoiceCap(const FGameplayTag& Category) const;
};
