// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "VO/Audio_VOTypes.h"
#include "Audio_VOBankDataAsset.generated.h"

/**
 * VO/BARK (4) content bank: maps VO-line identity tags to playable VO entries.
 *
 * Mirrors UAudio_SoundBankDataAsset: a tag-keyed map of soft sound refs + per-line metadata
 * (priority, category, bark cooldown, optional duck bus, speaker). Subclass of UDP_DataAsset, so each
 * bank is identified by its DataTag and indexed by the data registry. The VO subsystem loads one or
 * more banks and resolves PlayVO/TryBark by looking the line tag up across them.
 *
 * There is NO subtitle FText here — captions are produced by the requester and forwarded on the bus.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_VOBankDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Tag -> VO entry. Keys are VO-line identity tags (children of DP.Audio.VO). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (ForceInlineRow, Categories = "DP.Audio.VO"))
	TMap<FGameplayTag, FAudio_VOEntry> Lines;

	/** Resolve a single line by tag; nullptr if not present. Pure lookup; does not load the sound. */
	const FAudio_VOEntry* FindLine(const FGameplayTag& LineTag) const;

	//~ Begin UDP_DataAsset
	/** Own asset-manager bucket ("Audio_VOBank"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags lines with no Sound or a Category outside DP.Audio.Category. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
