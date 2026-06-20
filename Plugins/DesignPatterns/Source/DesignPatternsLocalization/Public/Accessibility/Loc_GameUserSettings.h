// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPGameUserSettings.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Loc_GameUserSettings.generated.h"

/**
 * Project game user settings subclass that adds the player-local accessibility option fields and
 * persists them through the engine's standard GameUserSettings.ini pipeline (alongside resolution,
 * vsync, scalability, etc.). Using UGameUserSettings rather than a bespoke save means the options
 * ride the engine's already-correct cross-platform persistence, are available before any world/save
 * exists (main menu, boot), and survive level travel and game restarts without a player save slot.
 *
 * The Localization accessibility subsystem treats this object as the persistence backing store for the
 * authoritative FSeam_AccessibilityOptions: it reads on init and writes (SaveSettings) on change.
 *
 * It derives from the core UDP_GameUserSettings so a project can point GameUserSettingsClassName at THIS
 * single class and obtain both the core player prefs (master volume, invert-Y) AND the accessibility
 * fields — there is only ever one active GameUserSettings class engine-wide.
 *
 * To make the engine instantiate THIS class instead of the stock UGameUserSettings, the host project
 * sets the class in DefaultEngine.ini:
 *
 *   [/Script/Engine.Engine]
 *   GameUserSettingsClassName=/Script/DesignPatternsLocalization.Loc_GameUserSettings
 *
 * If the project does NOT set this, the subsystem still works: it falls back to an in-memory copy of
 * the options (documented defensive fallback) and simply will not persist across runs. That keeps the
 * Localization module independently removable / inert by default.
 */
UCLASS(BlueprintType, Config = GameUserSettings, ConfigDoNotCheckDefaults)
class DESIGNPATTERNSLOCALIZATION_API ULoc_GameUserSettings : public UDP_GameUserSettings
{
	GENERATED_BODY()

public:
	ULoc_GameUserSettings();

	/**
	 * Convenience accessor that resolves the active GameUserSettings and casts to this class, or null
	 * if the project did not configure GameUserSettingsClassName to this type. Callers must null-check.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static ULoc_GameUserSettings* GetLocGameUserSettings();

	//~ Begin UGameUserSettings
	/** Reset accessibility fields to the struct's documented defaults alongside the engine reset. */
	virtual void SetToDefaults() override;

	/**
	 * Applies the engine settings then broadcasts the accessibility options to the accessibility
	 * subsystem so consumers update in lockstep with a settings-screen "Apply" press. bCheckForCommandLineOverrides
	 * matches the base signature; we forward it untouched to the engine settings.
	 */
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	//~ End UGameUserSettings

	/** Pack the individual config-persisted fields into the seam option struct consumers understand. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	FSeam_AccessibilityOptions GetAccessibilityOptions() const;

	/**
	 * Unpack a seam option struct into the individual config-persisted fields. Does NOT save to disk or
	 * broadcast — call SaveSettings()/ApplySettings() afterwards. Returns true if any field changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetAccessibilityOptions(const FSeam_AccessibilityOptions& InOptions);

protected:
	/**
	 * Pushes the current accessibility options into the GameInstance accessibility subsystem (if one
	 * exists for the supplied world context). Safe to call when no subsystem is reachable — it no-ops.
	 * Kept protected so only ApplySettings drives the broadcast, preserving a single ordering guarantee.
	 */
	void BroadcastAccessibilityOptions(const UObject* WorldContextObject) const;

	// --- Persisted accessibility fields (mirror FSeam_AccessibilityOptions). Each is Config so it lands
	//     in GameUserSettings.ini. Defaults mirror the struct's documented defaults. ---

	/** Show subtitles/captions. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	bool bSubtitlesEnabled = true;

	/** Subtitle size preset. Stored as the underlying uint8 for stable .ini serialization. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	ESeam_SubtitleSize SubtitleSize = ESeam_SubtitleSize::Medium;

	/** Draw a solid background behind subtitles for readability. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	bool bSubtitleBackground = true;

	/** Colorblind palette-remap mode. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	ESeam_ColorblindMode ColorblindMode = ESeam_ColorblindMode::None;

	/** Global UI scale multiplier. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float UIScale = 1.0f;

	/** Treat hold inputs as toggles. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	bool bHoldToToggle = false;

	/** Screen shake multiplier (0 disables shake). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ScreenShakeScale = 1.0f;

	/** Route narrative/UI text to a text-to-speech backend when available. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Accessibility")
	bool bTextToSpeechEnabled = false;
};
