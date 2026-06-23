// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subtitle/Loc_SubtitleTypes.h"
#include "Loc_RichSubtitleTypes.generated.h"

/** Screen anchor a rich subtitle prefers (the UI maps these to layout slots). */
UENUM(BlueprintType)
enum class ELoc_SubtitleAnchor : uint8
{
	/** Default bottom-center caption band. */
	BottomCenter,
	/** Top-center (e.g. for narrator / system lines). */
	TopCenter,
	/** Bottom-left (e.g. left-side speaker). */
	BottomLeft,
	/** Bottom-right (e.g. right-side speaker). */
	BottomRight
};

/**
 * Designer-authored presentation style for one speaker: the display-name key, color, screen anchor, font
 * role, and optional portrait. Resolved (with accessibility overrides applied) into FLoc_ResolvedSubtitleStyle.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_SpeakerStyle
{
	GENERATED_BODY()

	/** Localization key tag for this speaker's display name (resolved via the localization subsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FGameplayTag DisplayNameKey;

	/** Speaker name color (pre-accessibility). The rich subsystem may remap it for colorblind modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FLinearColor NameColor = FLinearColor::White;

	/** Screen anchor this speaker's lines prefer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	ELoc_SubtitleAnchor Anchor = ELoc_SubtitleAnchor::BottomCenter;

	/** UI font-role tag (DP.Loc.Font.Subtitle by default) the UI uses to resolve the caption font. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	FGameplayTag FontRole;

	/** Optional soft portrait the UI may show beside the line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Subtitle")
	TSoftObjectPtr<UObject> Portrait;
};

/**
 * The fully-resolved presentation for a surfaced line: the speaker display name (FText), the
 * accessibility-corrected color, anchor, font role, portrait, plus the accessibility size/background
 * flags. The UI binds these directly — no further lookups needed.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_ResolvedSubtitleStyle
{
	GENERATED_BODY()

	/** The localized speaker display name (empty for unattributed lines). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FText SpeakerName;

	/** Accessibility-corrected name color. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FLinearColor NameColor = FLinearColor::White;

	/** Resolved screen anchor. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	ELoc_SubtitleAnchor Anchor = ELoc_SubtitleAnchor::BottomCenter;

	/** Resolved font-role tag (defaults to DP.Loc.Font.Subtitle). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FGameplayTag FontRole;

	/** Optional portrait soft ref. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	TSoftObjectPtr<UObject> Portrait;

	/** Whether to draw a readability background (from accessibility options). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	bool bDrawBackground = true;
};

/**
 * One entry in the subtitle BACKLOG / history: the line plus when it was shown and its resolved style, so a
 * "subtitle history" UI can render past lines exactly as they appeared. Plain value struct (no UObject refs).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_SubtitleHistoryEntry
{
	GENERATED_BODY()

	/** The source line (speaker / text / priority). */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FLoc_SubtitleLine Line;

	/** The resolved presentation style at the time it was shown. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	FLoc_ResolvedSubtitleStyle Style;

	/** Game time (seconds, world-clock) the line was surfaced — for ordering / timestamps. */
	UPROPERTY(BlueprintReadOnly, Category = "Localization|Subtitle")
	double TimestampSeconds = 0.0;
};

/**
 * Bus payload broadcast by the UI when the focused widget changes, consumed by the accessibility focus
 * router to route the focus text to TTS. FText + a category tag (e.g. button vs heading) only — no UObject.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_UIFocusEvent
{
	GENERATED_BODY()

	/** The accessible text of the newly-focused widget (label / value). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Accessibility")
	FText FocusText;

	/** Optional category tag describing the focused element (drives TTS routing / ducking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Accessibility")
	FGameplayTag CategoryTag;

	/** Optional input-action tag whose binding label the router prepends (resolved via the glyph seam). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Accessibility")
	FGameplayTag ActionTag;
};
