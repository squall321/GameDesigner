// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/DPGameUserSettings.h"
#include "Core/DPLog.h"
#include "Engine/Engine.h"

UDP_GameUserSettings::UDP_GameUserSettings()
{
	MasterVolume = 1.0f;
	bInvertYAxis = false;
}

UDP_GameUserSettings* UDP_GameUserSettings::GetDPGameUserSettings()
{
	// Resolves to this class only if the project set GameUserSettingsClass accordingly.
	if (GEngine)
	{
		return Cast<UDP_GameUserSettings>(GEngine->GetGameUserSettings());
	}
	return nullptr;
}

void UDP_GameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	MasterVolume = 1.0f;
	bInvertYAxis = false;
}

void UDP_GameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	Super::ApplySettings(bCheckForCommandLineOverrides);

	// Push DP prefs to their consumers here (audio mix, input modifiers...). The plugin keeps
	// this side-effect-free by design; games override or react to the applied values.
	UE_LOG(LogDPSave, Verbose, TEXT("Applied DP user settings: MasterVolume=%.2f InvertY=%s."),
		MasterVolume, bInvertYAxis ? TEXT("true") : TEXT("false"));
}

void UDP_GameUserSettings::SetMasterVolume(float InVolume)
{
	MasterVolume = FMath::Clamp(InVolume, 0.0f, 1.0f);
}

void UDP_GameUserSettings::SetInvertYAxis(bool bInInvert)
{
	bInvertYAxis = bInInvert;
}
