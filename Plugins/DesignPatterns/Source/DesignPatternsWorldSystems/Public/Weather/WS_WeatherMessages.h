// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WS_WeatherMessages.generated.h"

/**
 * Bus payload broadcast on DP.Bus.WorldSystems.WeatherChanged whenever the active weather state changes
 * (server and clients, after replication). Cosmetic listeners (audio, lighting, post-process) react to
 * this by tag without depending on the weather subsystem's concrete type. Carries the resolved cosmetic
 * parameters so a listener needs no second lookup.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_WeatherChangedMessage
{
	GENERATED_BODY()

	/** The weather state now active (child of WS.Weather.State). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FGameplayTag NewStateTag;

	/** The weather state being left (invalid if this is the first state). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FGameplayTag PreviousStateTag;

	/** Target cosmetic intensity [0,1] of the new state (the value the live blend is heading toward). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	float TargetIntensity = 0.f;

	/** Wind vector (cm/s, world space) of the new state. */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FVector WindVector = FVector::ZeroVector;

	/** Particle VFX tag advertised by the new state (child of WS.Vfx, may be invalid). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FGameplayTag ParticleVfxTag;

	/** Ambient sound tag advertised by the new state (may be invalid). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FGameplayTag AmbientSoundTag;

	/** Blend duration (seconds) over which the transition is occurring. 0 = instant cut. */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	float TransitionSeconds = 0.f;
};

/**
 * Bus payload broadcast on DP.Bus.WorldSystems.RequestWeather to ask the weather subsystem to enter a
 * state by tag without resolving it directly. The subsystem applies the request only on authority;
 * clients ignore it (the resulting state replicates back down). This is a convenience entry point for
 * gameplay/AI/director code that already has the bus.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_RequestWeatherMessage
{
	GENERATED_BODY()

	/** The weather state to enter (child of WS.Weather.State). */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	FGameplayTag RequestedStateTag;

	/** When true, skip the blend and cut instantly to the requested state. */
	UPROPERTY(BlueprintReadOnly, Category = "WorldSystems|Weather")
	bool bInstant = false;
};
