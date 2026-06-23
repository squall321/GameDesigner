// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "DPUIEasingDataAsset.generated.h"

class UCurveFloat;

/**
 * A named, designer-authored library of easing curves, resolved by tag.
 *
 * The widget anim driver never hardcodes an easing function: callers (or data-driven widgets)
 * reference an easing by FGameplayTag, and this asset maps that tag to a UCurveFloat. This keeps
 * the "feel" of every fade/slide/scale fully in the hands of designers and consistent across the
 * project. Identity is the inherited DataTag (resolved through the data registry like every other
 * UDP_DataAsset).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_UIEasingDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Tag -> easing curve. A curve is expected to map normalized input progress (0..1) to an eased
	 * output (typically 0..1, but overshoot curves for "back"/"elastic" easings may exceed it). Used
	 * by UDP_WidgetAnimDriver to look up an easing by an opaque designer tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Easing")
	TMap<FGameplayTag, TObjectPtr<UCurveFloat>> Curves;

	/** Resolve the curve for EaseTag, or null when unmapped (caller falls back to linear). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Easing")
	UCurveFloat* ResolveCurve(FGameplayTag EaseTag) const;

	//~ Begin UDP_DataAsset
	/** Collapse every easing library into one asset-manager bucket for cheap discovery. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
