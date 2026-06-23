// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "Loc_VoiceBankDataAsset.generated.h"

class USoundBase;

/**
 * One designer-authored voice line within a culture's VO bank.
 *
 * Carries SOFT references only (so the bank itself never force-loads audio): the localized clip, an
 * optional baked viseme/lip-sync curve asset, and the subtitle key tag whose FText the caption system
 * resolves. The voice subsystem async-loads SoundAsset, reads its real duration AFTER load, and publishes
 * a subtitle line with that concrete duration so caption timing matches the audio.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_VoiceLineRow
{
	GENERATED_BODY()

	/** Soft reference to the localized VO clip for this line in this culture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Voice", meta = (AllowedClasses = "/Script/Engine.SoundBase"))
	TSoftObjectPtr<USoundBase> SoundAsset;

	/**
	 * Optional soft reference to a baked viseme / facial-animation curve asset handed to the lip-sync seam.
	 * Left as a generic UObject soft ref so the project decides the concrete curve type (a UCurveFloat, a
	 * curve table, or a middleware asset) without this module depending on it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Voice")
	TSoftObjectPtr<UObject> LipSyncCurve;

	/**
	 * Subtitle/caption key tag the subtitle system resolves to FText (via ULoc_LocalizationSubsystem).
	 * Empty means "no caption for this line" (VO plays without a subtitle).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Voice")
	FGameplayTag SubtitleKey;

	/**
	 * Speaker tag attributed to the surfaced subtitle (grouping / speaker-name / clear-by-speaker). Empty
	 * surfaces an unattributed caption.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Voice")
	FGameplayTag Speaker;

	/**
	 * Subtitle priority tag (anchored under DP.Loc.Subtitle.Priority). Empty falls back to standard dialogue
	 * priority via the subtitle subsystem's defensive default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Voice")
	FGameplayTag SubtitlePriority;
};

/**
 * A per-culture bank of voice lines. The voice subsystem selects the bank whose Culture matches the active
 * culture (exact, then language-prefix fallback) on culture change, and resolves a line id to its row.
 *
 * Identity is the inherited DataTag (resolved like any UDP_DataAsset). Data-driven: no magic clip names or
 * durations live in code — everything is here or read from the loaded clip at runtime.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Voice Bank"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_VoiceBankDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Culture code this bank supplies VO for (e.g. "en", "fr-FR"). The subsystem matches the active culture
	 * against this exactly first, then by language prefix (so "en-US" can fall back to an "en" bank).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Voice")
	FString Culture;

	/** Line id -> row. Line ids are opaque designer tags shared with whatever triggers VO (dialogue/quest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Voice")
	TMap<FGameplayTag, FLoc_VoiceLineRow> Lines;

	/** Find the row for LineId, or null if this bank has no such line. */
	const FLoc_VoiceLineRow* FindRow(FGameplayTag LineId) const;

	/** This bank's culture code (convenience accessor). */
	UFUNCTION(BlueprintPure, Category = "Localization|Voice")
	FString GetCulture() const { return Culture; }

	/** Collect the soft paths of every clip in this bank (for bulk preloading a culture if desired). */
	UFUNCTION(BlueprintPure, Category = "Localization|Voice")
	TArray<FSoftObjectPath> GetLineSoundPaths() const;

	//~ Begin UDP_DataAsset
	/** Collapse all voice banks into one asset-manager bucket so a project can scan them as a family. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject
	/** Flags an empty Culture or a row with no SoundAsset (a caption-only row is allowed but warned). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
