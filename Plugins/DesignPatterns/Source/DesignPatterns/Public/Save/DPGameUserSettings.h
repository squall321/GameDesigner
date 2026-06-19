// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "DPGameUserSettings.generated.h"

/**
 * DesignPatterns example of project-owned runtime player preferences.
 *
 * UGameUserSettings is the engine's per-machine, non-gameplay settings store (resolution,
 * vsync, scalability...) persisted to GameUserSettings.ini — distinct from save GAME data.
 * This subclass adds a couple of gameplay-adjacent prefs to demonstrate the seam: override
 * the engine's accessor (UDP_GameUserSettings::Get) by setting GameUserSettingsClass in your
 * project's DefaultEngine.ini under [/Script/Engine.Engine].
 *
 * ApplySettings is overridden so custom prefs are pushed to whatever consumes them whenever
 * the engine applies settings.
 */
UCLASS(Config = GameUserSettings, configdonotcheckdefaults)
class DESIGNPATTERNS_API UDP_GameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UDP_GameUserSettings();

	/** Convenience typed accessor for the active instance (nullptr if engine not ready). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Settings", meta = (DisplayName = "Get DP Game User Settings"))
	static UDP_GameUserSettings* GetDPGameUserSettings();

	//~ Begin UGameUserSettings
	/** Reset all DP prefs (and engine settings) to their defaults. */
	virtual void SetToDefaults() override;
	/** Apply engine settings, then push DP prefs to their consumers. */
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	//~ End UGameUserSettings

	// ---- Example player preferences ----

	/** Master gameplay-audio volume, 0..1. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Settings")
	void SetMasterVolume(float InVolume);

	/** Current master gameplay-audio volume, 0..1. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Settings")
	float GetMasterVolume() const { return MasterVolume; }

	/** Whether to invert the look (pitch) axis. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Settings")
	void SetInvertYAxis(bool bInInvert);

	/** Whether the look (pitch) axis is inverted. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Settings")
	bool GetInvertYAxis() const { return bInvertYAxis; }

private:
	/** Master gameplay-audio volume (0..1). Persisted to GameUserSettings.ini. */
	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	/** Invert look pitch. Persisted to GameUserSettings.ini. */
	UPROPERTY(Config)
	bool bInvertYAxis = false;
};
