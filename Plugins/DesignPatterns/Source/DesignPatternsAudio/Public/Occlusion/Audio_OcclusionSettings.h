// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/DeveloperSettings.h"
#include "Audio_OcclusionSettings.generated.h"

/**
 * OCCLUSION (1) project tunables. Project Settings -> Plugins -> Design Patterns Audio - Occlusion.
 *
 * Holds the genre-neutral knobs for the listener->source occlusion model: which collision channel to
 * trace, how often to sweep, how many sources to test per sweep (round-robin budget), and how an
 * occluded voice is attenuated (low-pass cutoff + volume multiplier) and how fast it eases. All values
 * are EditAnywhere/Config — there are no hardcoded magic numbers in code; the documented defensive
 * fallbacks below are only used when the CDO is somehow unreachable.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Audio - Occlusion"))
class DESIGNPATTERNSAUDIO_API UAudio_OcclusionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAudio_OcclusionSettings();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	/** Master switch: when false, the occlusion service performs no sweeps and components stay open. */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion")
	bool bEnableOcclusion = true;

	/** Collision channel traced from listener to source. Choose a channel walls/doors block. */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion")
	TEnumAsByte<ECollisionChannel> OcclusionTraceChannel = ECC_Visibility;

	/** Seconds between centralized sweeps. The service round-robins sources across sweeps. */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.02", UIMax = "1.0", Units = "s"))
	float SweepInterval = 0.2f;

	/** Maximum number of sources line-traced per sweep (the per-frame budget). Pure performance knob. */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "1", UIMin = "1", UIMax = "256"))
	int32 MaxSourcesPerSweep = 16;

	/**
	 * Beyond this distance (cm) from the nearest listener a source is not traced and is treated as
	 * un-occluded (it is too far to matter / would steal budget). 0 = no distance cull.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.0", UIMax = "20000.0", Units = "cm"))
	float MaxTraceDistance = 6000.f;

	/** Low-pass filter cutoff (Hz) applied to a FULLY-occluded voice (muffled-through-wall). */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "100.0", ClampMax = "20000.0", Units = "Hz"))
	float OccludedLowPassHz = 600.f;

	/** Linear volume multiplier applied to a FULLY-occluded voice (1 = no attenuation, 0 = silent). */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedVolumeMult = 0.5f;

	/**
	 * Easing speed (per second) at which a voice's occlusion factor interpolates toward its target, so
	 * walking behind cover fades rather than snaps. 0 = snap instantly.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.0", UIMax = "20.0"))
	float InterpSpeed = 6.f;

	/** Null-safe CDO accessor (never null in a running game). */
	static const UAudio_OcclusionSettings* Get();
};
