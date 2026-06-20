// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Loc_SubtitleTypes.generated.h"

/**
 * A single subtitle/caption line as requested by a producer (dialogue system, VO trigger, direct API).
 *
 * This is the wire-on-the-bus payload (sent as an FInstancedStruct on the dialogue/voice channels) and
 * the value the direct ShowSubtitle API takes. It is a plain value struct — no UObject refs — so it is
 * safe to copy across the message bus and hand to the ViewModel.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_SubtitleLine
{
	GENERATED_BODY()

	/**
	 * Who is speaking (a designer speaker tag, e.g. DP.Loc.Speaker.Narrator). Used for grouping,
	 * clear-by-speaker, and optional speaker-name presentation. May be empty for unattributed lines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FGameplayTag Speaker;

	/**
	 * The localized line text. Already an FText so it carries its own localization (callers can build it
	 * with ULoc_LocalizationSubsystem::FindText or any FText source). The subtitle system never converts
	 * this to a raw string for storage — FText all the way through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FText Text;

	/**
	 * Requested on-screen seconds. When <= 0 the subtitle subsystem computes a duration from text length
	 * times the per-character pacing in settings (clamped). > 0 uses this value verbatim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle", meta = (ForceUnits = "s"))
	float Duration = 0.f;

	/**
	 * Display priority tag (anchored under DP.Loc.Subtitle.Priority). Higher-priority lines show first
	 * and are evicted last when the on-screen cap is exceeded. Empty is treated as the standard dialogue
	 * priority by the subsystem's defensive fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FGameplayTag Priority;

	FLoc_SubtitleLine() = default;

	FLoc_SubtitleLine(const FGameplayTag& InSpeaker, const FText& InText, float InDuration = 0.f, const FGameplayTag& InPriority = FGameplayTag())
		: Speaker(InSpeaker)
		, Text(InText)
		, Duration(InDuration)
		, Priority(InPriority)
	{
	}
};

/**
 * One subtitle line as projected to the UI: the source line plus a stable instance id and the seconds
 * remaining on screen. The ViewModel exposes an array of these and the view renders them.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_ActiveSubtitleView
{
	GENERATED_BODY()

	/** Monotonic instance id assigned by the subsystem; stable for the life of this active line. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	int64 InstanceId = 0;

	/** The subtitle content being displayed. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FLoc_SubtitleLine Line;

	/** Seconds remaining before auto-dismiss. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	float TimeRemaining = 0.f;
};
