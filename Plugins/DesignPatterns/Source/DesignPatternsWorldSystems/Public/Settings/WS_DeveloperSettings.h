// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DeveloperSettings.h"
#include "WS_DeveloperSettings.generated.h"

class UWS_WeatherStateDataAsset;
class UWS_VfxBankDataAsset;

/**
 * Project-wide configuration for the DesignPatternsWorldSystems module. Appears under
 * Project Settings -> Plugins -> Design Patterns World Systems. Editing here requires no code.
 *
 * These are the genre-neutral tunables the weather subsystem and VFX manager fall back to: the
 * default VFX banks / weather-state assets to load, the default weather state to enter, transition
 * timing, the simulation-clock service key, and VFX pooling defaults. Every value is exposed via
 * UPROPERTY(EditAnywhere, Config); there are no hardcoded magic gameplay numbers in code. Each
 * subsystem reads the CDO via Get() and uses these as defensive fallbacks (documented inline at the
 * call sites). A null CDO is itself defended against — callers fall back to the field initializers
 * here, which the engine guarantees on the CDO.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns World Systems"))
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWS_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	// ---- Weather ----

	/**
	 * Weather-state assets the weather subsystem registers at world start so a request-by-tag does not
	 * stall on a synchronous load. Soft so the assets (and their soft VFX/sound refs) only cost what is
	 * actually referenced. A project may also register states at runtime via the data registry.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weather")
	TArray<TSoftObjectPtr<UWS_WeatherStateDataAsset>> DefaultWeatherStates;

	/**
	 * Weather state the subsystem enters on the server at world start (child of WS.Weather.State).
	 * If unset, or no matching state asset is registered, the subsystem stays in the inert "no weather"
	 * state until something requests one.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weather", meta = (Categories = "WS.Weather.State"))
	FGameplayTag DefaultWeatherStateTag;

	/**
	 * Fallback blend duration (seconds) used when transitioning between weather states whose asset does
	 * not specify its own transition time. The weather model interpolates intensity over this window so
	 * cosmetic responses can crossfade. Clamped non-negative; 0 means an instant cut.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weather", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "60.0"))
	float DefaultWeatherTransitionSeconds = 8.f;

	/**
	 * How often (seconds of real time) the weather subsystem advances its transition blend and samples
	 * the simulation clock. A small fixed cadence keeps the blend smooth without ticking every frame.
	 * Clamped to a sane lower bound at the call site so a misconfigured 0 cannot busy-loop a timer.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weather", meta = (ClampMin = "0.02", UIMin = "0.05", UIMax = "1.0"))
	float WeatherUpdateInterval = 0.2f;

	/**
	 * Service-locator key under which an authoritative simulation clock (ISeam_SimClock) is published.
	 * The weather subsystem resolves it weakly to drive time-of-day-aware blends; if unresolved the
	 * weather model simply runs in real time (documented inert default). Anchor under DP.Service.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weather", meta = (Categories = "DP.Service"))
	FGameplayTag SimClockServiceKey;

	// ---- VFX ----

	/**
	 * VFX banks the VFX manager registers at GameInstance start so a spawn-by-tag resolves synchronously
	 * against an in-memory index (the system asset itself is still soft-loaded on first use). Soft so
	 * the bank's referenced systems only cost what is actually spawned.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Vfx")
	TArray<TSoftObjectPtr<UWS_VfxBankDataAsset>> DefaultVfxBanks;

	/**
	 * Default number of carrier actors to warm up per pooled VFX entry that requests pooling. A bank
	 * entry may override this with its own pre-warm hint. The defensive lower bound (>= 0) is applied at
	 * the call site. 0 disables pre-warm for entries that do not specify their own.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Vfx", meta = (ClampMin = "0", UIMin = "0", UIMax = "64"))
	int32 DefaultVfxPoolWarmup = 0;

	/**
	 * Hard cap on simultaneously-tracked attached/looping VFX handles the manager retains for StopVfx.
	 * One-shots are not tracked. When exceeded the manager reclaims the oldest tracked handle. The
	 * manager clamps this to a positive lower bound if somehow non-positive (documented at the call site).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Vfx", meta = (ClampMin = "1", UIMin = "8", UIMax = "512"))
	int32 MaxTrackedVfx = 128;

	/**
	 * Seconds after which a tracked one-shot's carrier is auto-released back to the pool if its system
	 * does not report completion (a safety net so a non-looping system that never fires its finished
	 * callback cannot leak a carrier). Clamped to a positive lower bound at the call site.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Vfx", meta = (ClampMin = "0.5", UIMin = "1.0", UIMax = "120.0"))
	float OneShotReclaimSeconds = 30.f;

	/** Convenience accessor (never null in a running game; the CDO is populated from the project ini). */
	static const UWS_DeveloperSettings* Get();
};
