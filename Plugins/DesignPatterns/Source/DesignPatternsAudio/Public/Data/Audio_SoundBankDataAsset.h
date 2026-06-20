// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Audio_SoundBankDataAsset.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * One sound entry in a bank: the data that fully describes how to play a single tag-keyed sound.
 *
 * Asset references are SOFT (TSoftObjectPtr) so a bank can index hundreds of sounds without keeping
 * any of them loaded; the sound manager async-loads an entry's Sound the first time its tag is
 * requested. There are NO hardcoded asset references anywhere in code — every reference lives in a
 * data asset edited by designers.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_SoundEntry
{
	GENERATED_BODY()

	/** The sound to play. Soft so the bank stays unloaded until a tag is actually requested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundBase> Sound;

	/**
	 * Optional spatialization settings used by PlaySoundAtLocation. Null = use the engine/asset
	 * default attenuation. Ignored by 2D playback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundAttenuation> Attenuation;

	/**
	 * Category this sound belongs to (child of DP.Audio.Category). Drives concurrency limiting,
	 * category-volume scaling, ducking and StopCategory. An invalid tag means "uncategorized" — such
	 * voices are unlimited and unaffected by category volume.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (Categories = "DP.Audio.Category"))
	FGameplayTag Category;

	/**
	 * Maximum simultaneous voices of THIS sound entry within its category before virtualization
	 * (oldest-steal) kicks in. <= 0 means "no per-entry limit" (only the category-wide voice budget
	 * from settings applies). A pure designer tunable — no magic numbers in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0", UIMin = "0", UIMax = "32"))
	int32 MaxConcurrent = 0;

	/** Default linear volume for this sound (multiplied by the per-call VolumeMult and category volume). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float DefaultVolume = 1.f;
};

/**
 * A data-driven bank mapping sound-identity tags to playable sound entries.
 *
 * Subclass of UDP_DataAsset, so each bank is identified by its DataTag and indexed by the data
 * registry. The sound manager loads one or more banks (configured in Audio_DeveloperSettings or
 * pushed at runtime) and resolves IAudio_AudioController::PlaySound* calls by looking SoundTag up
 * across the loaded banks' Entries maps.
 *
 * Designers add sounds purely by editing this asset; code never names a USoundBase directly.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_SoundBankDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Tag -> entry. Keys are sound-identity tags (children of DP.Audio.Sound). A tag present in
	 * multiple loaded banks is resolved by load order (first loaded bank wins); duplicates are logged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ForceInlineRow, Categories = "DP.Audio.Sound"))
	TMap<FGameplayTag, FAudio_SoundEntry> Entries;

	/**
	 * Resolve a single entry by tag. Returns nullptr if the tag is not present in this bank.
	 * Pure lookup; does not load the referenced sound.
	 */
	const FAudio_SoundEntry* FindEntry(const FGameplayTag& SoundTag) const;

	//~ Begin UDP_DataAsset
	/** Collapse all sound banks into one asset-manager type bucket ("Audio_SoundBank"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags entries with an unset Sound or a Category outside DP.Audio.Category. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
