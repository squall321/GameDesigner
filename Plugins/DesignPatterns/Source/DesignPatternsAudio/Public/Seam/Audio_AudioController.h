// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Audio_AudioController.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UAudio_AudioController : public UInterface
{
	GENERATED_BODY()
};

/**
 * "Play by tag" audio facade — the cosmetic-audio seam other systems use without depending on a
 * concrete audio class.
 *
 * The concrete implementation is the GameInstance-scoped UAudio_SoundManagerSubsystem, which
 * registers itself into the service locator under AudioNativeTags::Service_Audio (DP.Service.Audio,
 * WeakObserved). Consumers (HUD, gameplay, interaction, narrative...) resolve a
 * TScriptInterface<IAudio_AudioController> from UDP_ServiceLocatorSubsystem and call these methods;
 * they never hard-include this module's concrete subsystem.
 *
 * Everything here is LOCAL/COSMETIC and is never replicated: audio is produced from already-
 * replicated gameplay (directly, or via DP.Bus.* messages) and played on each machine independently.
 *
 * Sound and Category identities are FGameplayTags resolved out of data-driven sound banks
 * (UAudio_SoundBankDataAsset); there are no hardcoded asset references behind this interface.
 */
class DESIGNPATTERNSAUDIO_API IAudio_AudioController
{
	GENERATED_BODY()

public:
	/**
	 * Play a non-spatialized 2D one-shot resolved from the sound banks by SoundTag.
	 *
	 * Honours the sound's category concurrency limit (oldest-steal virtualization when the cap is
	 * hit), the category volume multiplier and any active ducking. No-op (safe) on a headless / no-
	 * audio-device build, when the tag does not resolve, or before the asset finishes async loading
	 * (the play is deferred to load completion).
	 *
	 * @param SoundTag    Sound identity (child of DP.Audio.Sound). Must resolve in a loaded bank.
	 * @param VolumeMult  Per-call linear volume multiplier (1.0 = the bank entry's default volume).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio")
	void PlaySound2D(FGameplayTag SoundTag, float VolumeMult = 1.f);

	/**
	 * Play a spatialized one-shot at a world location, resolved from the sound banks by SoundTag.
	 *
	 * Uses the bank entry's attenuation (if any). Subject to the same category concurrency,
	 * category-volume and ducking rules as PlaySound2D, and is equally safe to call headless / on an
	 * unresolved tag / before async load completes.
	 *
	 * @param SoundTag    Sound identity (child of DP.Audio.Sound).
	 * @param Location    World-space position to play at.
	 * @param VolumeMult  Per-call linear volume multiplier (1.0 = the bank entry's default volume).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio")
	void PlaySoundAtLocation(FGameplayTag SoundTag, FVector Location, float VolumeMult = 1.f);

	/**
	 * Stop every currently-active voice belonging to Category (or any child category, via tag
	 * hierarchy). Used e.g. to cut all UI sounds on screen close or all combat SFX on encounter end.
	 *
	 * @param Category  Category root or leaf (child of DP.Audio.Category).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio")
	void StopCategory(FGameplayTag Category);

	/**
	 * Set the runtime linear volume multiplier applied to every voice in Category (and, by tag
	 * hierarchy, its child categories). Persists until changed; applied to both currently-playing
	 * and future voices in that category. Clamped to a non-negative range by the implementation.
	 *
	 * @param Category  Category root or leaf (child of DP.Audio.Category).
	 * @param Volume    New linear multiplier (1.0 = unattenuated, 0.0 = muted).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Audio")
	void SetCategoryVolume(FGameplayTag Category, float Volume);
};
