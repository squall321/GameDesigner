// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "WS_WeatherStateDataAsset.generated.h"

/**
 * Data-driven description of ONE weather state (Clear / Rain / Storm / Fog / Snow ...).
 *
 * A weather state is identified by StateTag (a child of WS.Weather.State) and bundles the genre-neutral,
 * purely COSMETIC parameters the weather subsystem blends toward and dispatches by tag: a target
 * intensity, a wind vector, and the VFX / ambient-sound tags that drive the local response through the
 * shared VFX controller seam and the message bus. The asset references nothing concrete (no Niagara
 * system, no sound) directly — only tags — so it never pulls a cosmetic dependency into gameplay code.
 *
 * Resolved against the data registry by StateTag (which is mirrored onto the base DataTag in
 * PostLoad so registry lookups and weather requests share one key).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_WeatherStateDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UWS_WeatherStateDataAsset();

	/**
	 * Identity of this weather state (child of WS.Weather.State). This is the key the weather subsystem
	 * stores as the authoritative current state and the key cosmetic listeners react to. Mirrored onto
	 * the base DataTag so the data registry indexes the asset under the same tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (Categories = "WS.Weather.State"))
	FGameplayTag StateTag;

	/**
	 * Target cosmetic intensity for this state in [0,1] (e.g. rain density, fog thickness). The weather
	 * subsystem blends the live intensity toward this over the transition window so listeners can scale
	 * their effect strength. Purely cosmetic; never gameplay-affecting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Intensity = 1.f;

	/**
	 * Wind direction and strength (cm/s) for this state in world space. Surfaced to cosmetic systems
	 * (particles, foliage) via the weather-changed message so they can orient/scale wind-driven effects.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FVector WindVector = FVector::ZeroVector;

	/**
	 * Tag of the looping particle effect representing this weather (child of WS.Vfx). Dispatched by the
	 * weather subsystem through ISeam_VfxController; if the tag is unset or unresolved no particle plays
	 * (documented inert default). Resolved against a WS_VfxBankDataAsset by the VFX manager.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (Categories = "WS.Vfx"))
	FGameplayTag ParticleVfxTag;

	/**
	 * Tag of the looping ambient sound for this weather (e.g. DP.Audio.Sound.Ambience.Rain). Surfaced on
	 * the weather-changed message so an audio system (resolved elsewhere) can play it by tag. This module
	 * does not play audio itself — it only advertises the tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (Categories = "DP.Audio.Sound"))
	FGameplayTag AmbientSoundTag;

	/**
	 * Blend duration (seconds) to use when transitioning INTO this state. A negative value means "use the
	 * project default" (UWS_DeveloperSettings::DefaultWeatherTransitionSeconds); 0 is an instant cut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (UIMin = "-1.0", UIMax = "60.0"))
	float TransitionSeconds = -1.f;

	/** True if this state declares its own transition duration (TransitionSeconds >= 0). */
	UFUNCTION(BlueprintPure, Category = "Weather")
	bool HasExplicitTransition() const { return TransitionSeconds >= 0.f; }

	//~ Begin UObject
	/** Mirror StateTag onto the base DataTag so registry lookups and weather requests share one key. */
	virtual void PostLoad() override;
#if WITH_EDITOR
	/** Keep DataTag in sync with StateTag while editing. */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject
};
