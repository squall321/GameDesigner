// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Seam_AccessibilityTypes.generated.h"

/** Colorblind palette-remap modes. */
UENUM(BlueprintType)
enum class ESeam_ColorblindMode : uint8
{
	None,
	Protanopia,
	Deuteranopia,
	Tritanopia
};

/** Subtitle text size presets. */
UENUM(BlueprintType)
enum class ESeam_SubtitleSize : uint8
{
	Small,
	Medium,
	Large,
	ExtraLarge
};

/**
 * The full set of player accessibility options, broadcast to every ISeam_AccessibilityConsumer when any
 * value changes so UI/Camera/HUD react uniformly. Persisted via the game user settings / save system.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_AccessibilityOptions
{
	GENERATED_BODY()

	/** Show subtitles/captions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	bool bSubtitlesEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	ESeam_SubtitleSize SubtitleSize = ESeam_SubtitleSize::Medium;

	/** Draw a solid background behind subtitles for readability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	bool bSubtitleBackground = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	ESeam_ColorblindMode ColorblindMode = ESeam_ColorblindMode::None;

	/** Global UI scale multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float UIScale = 1.0f;

	/** Treat hold inputs as toggles (accessibility for sustained presses). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	bool bHoldToToggle = false;

	/** Reduce or disable screen shake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScreenShakeScale = 1.0f;

	/** Route narrative/UI text to a text-to-speech backend when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Accessibility")
	bool bTextToSpeechEnabled = false;
};
