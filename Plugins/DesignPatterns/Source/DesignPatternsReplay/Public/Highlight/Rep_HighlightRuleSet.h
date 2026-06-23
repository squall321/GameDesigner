// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Rep_HighlightRuleSet.generated.h"

/**
 * One data-driven rule the highlight detector evaluates against the timeline. A rule fires when at
 * least MinEventCount events whose tag matches TriggerTag occur within WindowSeconds of each other;
 * the resulting moment is tagged KindTag and scored from BaseScore + per-event PerEventScore.
 *
 * Every threshold is a designer-authored field (no magic numbers in code).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_HighlightRule
{
	GENERATED_BODY()

	/** The timeline event tag(s) this rule keys on; a rule matches Rep.Event.* or game-authored tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight")
	FGameplayTag TriggerTag;

	/** The kind tag stamped onto a produced moment (child of Rep.Highlight.*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (Categories = "Rep.Highlight"))
	FGameplayTag KindTag;

	/** Sliding-window length (seconds) the events must cluster within to count as one moment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float WindowSeconds = 4.f;

	/** Minimum number of matching events inside the window to fire (e.g. 2 for a double-kill). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "1"))
	int32 MinEventCount = 2;

	/** Base score awarded when this rule fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.0"))
	float BaseScore = 1.f;

	/** Additional score per contributing event beyond the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.0"))
	float PerEventScore = 0.5f;

	/** True when this rule is fully specified. */
	bool IsUsable() const { return TriggerTag.IsValid() && KindTag.IsValid() && MinEventCount >= 1 && WindowSeconds > 0.f; }
};

/**
 * URep_HighlightRuleSet — the data-driven home for highlight detection thresholds and clip framing.
 *
 * The detector (URep_HighlightDetector) reads this asset to decide which clusters of timeline events
 * become highlight moments and how to score/frame them. NOTHING in the highlight code hard-codes a
 * window length, score or clip pad — they all live here (or on the developer settings for global pads).
 *
 * Resolved by the highlight subsystem from the URep_DeveloperSettings::HighlightRuleSet soft ref.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_HighlightRuleSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The ordered detection rules. First matching rule (by tag) wins for a given cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight")
	TArray<FRep_HighlightRule> Rules;

	/**
	 * Default clip lead-in (seconds before a moment's anchor) when the developer-settings pad is not
	 * used. Defensive fallback so a moment always has a sensible window even with a minimal asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.0"))
	float DefaultClipLeadInSeconds = 3.f;

	/** Default clip lead-out (seconds after a moment's anchor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.0"))
	float DefaultClipLeadOutSeconds = 2.f;

	/**
	 * The minimum score a produced moment must reach to be retained. Filters out trivially-firing rules.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Replay|Highlight", meta = (ClampMin = "0.0"))
	float MinScoreToRetain = 1.f;

	/** Find the first usable rule that keys on EventTag (exact or parent), or null. */
	const FRep_HighlightRule* FindRuleForEvent(const FGameplayTag& EventTag) const;

	/** Lead-in to use, preferring the explicit Settings override when positive. */
	float GetEffectiveLeadIn(float SettingsOverride) const
	{
		return (SettingsOverride > 0.f) ? SettingsOverride : FMath::Max(0.f, DefaultClipLeadInSeconds);
	}

	/** Lead-out to use, preferring the explicit Settings override when positive. */
	float GetEffectiveLeadOut(float SettingsOverride) const
	{
		return (SettingsOverride > 0.f) ? SettingsOverride : FMath::Max(0.f, DefaultClipLeadOutSeconds);
	}

#if WITH_EDITOR
	//~ Begin UObject
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
