// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Subtitle/Loc_RichSubtitleTypes.h"
#include "Loc_SpeakerStyleDataAsset.generated.h"

/**
 * Maps speaker FGameplayTag -> FLoc_SpeakerStyle with hierarchy-aware fallback (a style on
 * DP.Loc.Speaker.NPC applies to DP.Loc.Speaker.NPC.Guard unless a more specific entry exists). The rich
 * subtitle subsystem resolves a line's speaker through this asset, then applies accessibility corrections.
 *
 * Identity is the inherited DataTag. No hardcoded colors / names — everything is authored here.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Speaker Styles"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_SpeakerStyleDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Speaker tag -> presentation style. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Subtitle")
	TMap<FGameplayTag, FLoc_SpeakerStyle> Styles;

	/**
	 * The style used for a line whose speaker matches no entry (and for unattributed lines). Lets a project
	 * guarantee a sane default color/anchor without an entry per speaker.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Subtitle")
	FLoc_SpeakerStyle DefaultStyle;

	/**
	 * Find the style for Speaker (exact, then most-specific ancestor, then DefaultStyle). Always returns a
	 * style via Out; the bool indicates whether a specific (non-default) entry matched.
	 *
	 * @param Speaker The line's speaker tag (may be invalid/empty -> DefaultStyle).
	 * @param Out     Receives the resolved style.
	 * @return true if a specific entry matched; false if DefaultStyle was used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	bool FindStyle(FGameplayTag Speaker, FLoc_SpeakerStyle& Out) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
