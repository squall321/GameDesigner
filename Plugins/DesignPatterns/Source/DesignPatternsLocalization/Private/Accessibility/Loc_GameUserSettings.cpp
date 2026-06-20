// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_GameUserSettings.h"

#include "Accessibility/Loc_AccessibilitySubsystem.h"
#include "Core/DPLog.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

ULoc_GameUserSettings::ULoc_GameUserSettings()
{
	// Field defaults are declared inline in the header (mirroring FSeam_AccessibilityOptions defaults);
	// nothing further to do here. The engine loads persisted .ini values over these on LoadSettings().
}

ULoc_GameUserSettings* ULoc_GameUserSettings::GetLocGameUserSettings()
{
	// UGameUserSettings::GetGameUserSettings() returns the engine's active instance, whose concrete class
	// is whatever GameUserSettingsClassName points at. If the project didn't set it to this subclass the
	// cast fails and we return null — every caller treats null as "no persistence backing", a documented
	// inert fallback.
	return Cast<ULoc_GameUserSettings>(GEngine ? GEngine->GetGameUserSettings() : nullptr);
}

void ULoc_GameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	// Mirror FSeam_AccessibilityOptions' documented defaults exactly so a "reset to defaults" in a
	// settings screen restores the same values the seam struct ships with.
	const FSeam_AccessibilityOptions Defaults;
	bSubtitlesEnabled   = Defaults.bSubtitlesEnabled;
	SubtitleSize        = Defaults.SubtitleSize;
	bSubtitleBackground = Defaults.bSubtitleBackground;
	ColorblindMode      = Defaults.ColorblindMode;
	UIScale             = Defaults.UIScale;
	bHoldToToggle       = Defaults.bHoldToToggle;
	ScreenShakeScale    = Defaults.ScreenShakeScale;
	bTextToSpeechEnabled= Defaults.bTextToSpeechEnabled;
}

FSeam_AccessibilityOptions ULoc_GameUserSettings::GetAccessibilityOptions() const
{
	FSeam_AccessibilityOptions Out;
	Out.bSubtitlesEnabled    = bSubtitlesEnabled;
	Out.SubtitleSize         = SubtitleSize;
	Out.bSubtitleBackground  = bSubtitleBackground;
	Out.ColorblindMode       = ColorblindMode;
	Out.UIScale              = UIScale;
	Out.bHoldToToggle        = bHoldToToggle;
	Out.ScreenShakeScale     = ScreenShakeScale;
	Out.bTextToSpeechEnabled = bTextToSpeechEnabled;
	return Out;
}

bool ULoc_GameUserSettings::SetAccessibilityOptions(const FSeam_AccessibilityOptions& InOptions)
{
	bool bChanged = false;

	auto AssignBool = [&bChanged](bool& Dst, bool Src)
	{
		if (Dst != Src) { Dst = Src; bChanged = true; }
	};
	auto AssignFloat = [&bChanged](float& Dst, float Src)
	{
		if (!FMath::IsNearlyEqual(Dst, Src)) { Dst = Src; bChanged = true; }
	};

	AssignBool(bSubtitlesEnabled,    InOptions.bSubtitlesEnabled);
	if (SubtitleSize != InOptions.SubtitleSize)      { SubtitleSize = InOptions.SubtitleSize;   bChanged = true; }
	AssignBool(bSubtitleBackground,  InOptions.bSubtitleBackground);
	if (ColorblindMode != InOptions.ColorblindMode)  { ColorblindMode = InOptions.ColorblindMode; bChanged = true; }
	AssignFloat(UIScale,             InOptions.UIScale);
	AssignBool(bHoldToToggle,        InOptions.bHoldToToggle);
	AssignFloat(ScreenShakeScale,    InOptions.ScreenShakeScale);
	AssignBool(bTextToSpeechEnabled, InOptions.bTextToSpeechEnabled);

	return bChanged;
}

void ULoc_GameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	// Apply the engine-owned settings (resolution, scalability, vsync, ...) first and unchanged.
	Super::ApplySettings(bCheckForCommandLineOverrides);

	// Then broadcast the accessibility options so UI/Camera/HUD update in lockstep with the Apply press.
	// We don't have an explicit world context here, so resolve one from the active game instance below.
	BroadcastAccessibilityOptions(/*WorldContextObject*/ nullptr);
}

void ULoc_GameUserSettings::BroadcastAccessibilityOptions(const UObject* WorldContextObject) const
{
	if (!GEngine)
	{
		return;
	}

	// Find a game-world game instance to host the subsystem lookup. Prefer an explicit context; otherwise
	// pick the first game/PIE world's game instance. This intentionally avoids touching editor/preview worlds.
	UGameInstance* GameInstance = nullptr;

	if (WorldContextObject)
	{
		if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull))
		{
			GameInstance = World->GetGameInstance();
		}
	}

	if (!GameInstance)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.OwningGameInstance)
			{
				GameInstance = Context.OwningGameInstance;
				break;
			}
		}
	}

	if (!GameInstance)
	{
		// No live game world (e.g. applied from a boot/menu flow before a world exists). The subsystem will
		// load the now-persisted values itself on Initialize, so dropping the broadcast here is harmless.
		return;
	}

	if (ULoc_AccessibilitySubsystem* Accessibility = GameInstance->GetSubsystem<ULoc_AccessibilitySubsystem>())
	{
		// Hand the freshly-applied option set to the subsystem; it pushes to consumers and fires its delegate.
		// SetOptions is a no-op if nothing actually changed, so repeated Applies are cheap.
		Accessibility->SetOptions(GetAccessibilityOptions());
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("Loc_GameUserSettings::ApplySettings: accessibility subsystem unavailable; broadcast skipped."));
	}
}
