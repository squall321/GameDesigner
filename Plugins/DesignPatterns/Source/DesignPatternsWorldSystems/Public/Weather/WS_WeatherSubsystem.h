// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Engine/TimerHandle.h"
#include "Vfx/Seam_VfxController.h"
#include "Weather/WS_WeatherCarrier.h"
#include "WS_WeatherSubsystem.generated.h"

class UWS_WeatherStateDataAsset;
class ISeam_SimClock;
struct FDP_Message;

/**
 * Fired locally (server and clients) when the active weather state changes, after the cosmetic response
 * has been kicked off. The weather subsystem is per-world, so this is the natural place gameplay/UI in
 * the same world binds to react to weather without touching the bus.
 * @param NewStateTag      The weather state now active.
 * @param PreviousStateTag The weather state being left (invalid if first).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWS_OnWeatherChanged,
	FGameplayTag, NewStateTag, FGameplayTag, PreviousStateTag);

/**
 * World-scoped tag-keyed WEATHER state model.
 *
 * Acts as a small state machine over weather-state tags (children of WS.Weather.State). The server's
 * authoritative SetWeather resolves the requested state's data asset, drives a tiny replicated AInfo
 * carrier (AWS_WeatherCarrier) to broadcast the lightweight current-state tag (subsystems never
 * replicate), and starts a blend toward the new state's cosmetic intensity. Both server and clients
 * receive the carrier's change on the same path and dispatch the LOCAL, COSMETIC response: a looping
 * particle effect by tag through the shared ISeam_VfxController seam, and the new state's parameters
 * (wind, ambient-sound tag) over the message bus for audio/lighting systems to consume.
 *
 * The model optionally rides the shared simulation clock (ISeam_SimClock, resolved weakly from the
 * service locator): while a clock is present the blend honours its time scale / pause so weather speeds
 * with simulation time; with no clock resolved it blends in real time (documented inert default). No
 * cosmetic class is hard-included — all coupling is via seams and the bus.
 */
UCLASS()
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_WeatherSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * World subsystems have no built-in authority query (UWorldSubsystem provides none). True on the
	 * server / standalone, false on a network client. Authoritative mutators gate on this.
	 */
	bool HasWorldAuthority() const { const UWorld* W = GetWorld(); return W && W->GetNetMode() != NM_Client; }

	// ---- Authority API ----

	/**
	 * Enter the weather state identified by StateTag. AUTHORITY ONLY — early-returns on clients (use the
	 * RequestWeather bus channel or a player-owned intent path from a client). Resolves the state's data
	 * asset (registering it lazily if needed), drives the replicated carrier, and starts the blend. A
	 * request for the already-active state is a no-op unless bForceRestart.
	 *
	 * @param StateTag       The weather state to enter (child of WS.Weather.State).
	 * @param bInstant       Skip the blend and cut instantly to full target intensity.
	 * @param bForceRestart  Re-apply even if StateTag is already active.
	 * @return True if a change was applied or restarted (false on clients / invalid tag / no-op).
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldSystems|Weather")
	bool SetWeather(FGameplayTag StateTag, bool bInstant = false, bool bForceRestart = false);

	// ---- Reads (client-safe) ----

	/** The weather state currently active (from the replicated carrier), or an invalid tag if none. */
	UFUNCTION(BlueprintPure, Category = "WorldSystems|Weather")
	FGameplayTag GetCurrentWeatherState() const;

	/**
	 * The live, blended cosmetic intensity in [0,1] — interpolated from the previous state's intensity
	 * toward the current state's target over the transition window. Cosmetic systems scale by this.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldSystems|Weather")
	float GetCurrentIntensity() const { return CurrentIntensity; }

	/** True while a transition blend is in progress (live intensity has not yet reached the target). */
	UFUNCTION(BlueprintPure, Category = "WorldSystems|Weather")
	bool IsTransitioning() const { return bTransitionActive; }

	/** Fired locally (server and clients) when the active weather state changes. */
	UPROPERTY(BlueprintAssignable, Category = "WorldSystems|Weather")
	FWS_OnWeatherChanged OnWeatherChanged;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	// ---- Setup ----

	/** Register the weather-state assets named in the developer settings into the data registry index. */
	void RegisterDefaultStatesFromSettings();

	/** Register this subsystem under DP.Service.Weather (weak) so other systems can resolve it by tag. */
	void RegisterAsService();

	/** Remove our service registration (called from Deinitialize). */
	void UnregisterAsService();

	/** Subscribe to DP.Bus.WorldSystems.RequestWeather so a game can request weather by tag via the bus. */
	void BindBusListeners();

	/**
	 * Spawn the single replicated weather carrier. Server-only; clients receive it via replication and
	 * bind to it lazily in EnsureCarrierBound.
	 */
	void EnsureCarrierSpawnedOnAuthority();

	/** Bind OnStateChanged on whichever carrier exists in this world (server's spawned one or a replicated one). */
	void EnsureCarrierBound();

	// ---- Reactions ----

	/** Carrier delegate: the replicated weather state changed (server and clients). Resolves + reacts. */
	UFUNCTION()
	void HandleCarrierStateChanged(AWS_WeatherCarrier* Carrier, FGameplayTag NewState);

	/** Apply the local cosmetic response for NewState: start the intensity blend, drive VFX, fire the bus. */
	void ApplyStateLocally(const FGameplayTag& NewState, bool bInstant);

	/** Bus handler for DP.Bus.WorldSystems.RequestWeather — applies on authority, ignored on clients. */
	void HandleRequestWeatherMessage(const FDP_Message& Message);

	// ---- Resolution helpers ----

	/** Resolve a weather-state asset by tag from the data registry (synchronous load on first use), or null. */
	UWS_WeatherStateDataAsset* ResolveState(const FGameplayTag& StateTag) const;

	/** Resolve the VFX controller seam (DP.Service.Vfx) from the locator, or an empty interface. */
	TScriptInterface<ISeam_VfxController> ResolveVfxController() const;

	/** Resolve the simulation clock seam weakly (re-resolved if the cached one went stale), or empty. */
	TScriptInterface<ISeam_SimClock> ResolveSimClock() const;

	// ---- Transition stepping ----

	/** Ensure the recurring blend/clock-sample timer is running at the configured cadence. */
	void EnsureUpdateTimer();

	/** Timer callback: advance the intensity blend by the (clock-scaled) elapsed time. */
	void StepTransition();

	/** Stop the looping weather particle VFX if one is active, returning its handle to the controller. */
	void StopActiveWeatherVfx();

	// ---- State ----

	/**
	 * The server's spawned carrier (authority) OR the replicated one we found on a client. Non-owning:
	 * the world owns the actor. Weak so a torn-down/destroyed carrier never dangles.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AWS_WeatherCarrier> Carrier;

	/** Cached resolved data for the active state (for blend targets and cosmetic params). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UWS_WeatherStateDataAsset> ActiveStateAsset;

	/**
	 * Cross-world simulation clock provider, held WEAKLY (a world subsystem must not hard-keep a possibly
	 * cross-world interface). Re-resolved on staleness. Empty means "run in real time". Mutable so the
	 * const ResolveSimClock() can lazily populate/refresh the cache.
	 */
	mutable TWeakInterfacePtr<ISeam_SimClock> CachedSimClock;

	/** The weather state we last reacted to locally (to compute previous-state for the change events). */
	FGameplayTag LastReactedState;

	/** Handle to the currently-playing looping weather particle effect (invalid = none). */
	UPROPERTY(Transient)
	FSeam_VfxHandle ActiveVfxHandle;

	/** Live blended cosmetic intensity in [0,1]. */
	float CurrentIntensity = 0.f;

	/** Intensity the blend started from (previous state's intensity, or current at retarget). */
	float BlendStartIntensity = 0.f;

	/** Intensity the blend is heading toward (new state's target). */
	float BlendTargetIntensity = 0.f;

	/** Total blend duration (seconds) for the active transition. 0 = instant. */
	float BlendDuration = 0.f;

	/** Elapsed (clock-scaled) blend time (seconds) since the transition started. */
	float BlendElapsed = 0.f;

	/** True while a transition blend is in progress. */
	bool bTransitionActive = false;

	/** Recurring timer driving StepTransition. */
	FTimerHandle UpdateTimerHandle;

	/** True once BindBusListeners has run (avoid double-subscribe across re-init paths). */
	bool bBusBound = false;

	/**
	 * Set by the authority SetWeather just before it drives the carrier, then consumed by the carrier's
	 * synchronous OnStateChanged on the server to decide whether to cut or blend. Always false on clients
	 * (they blend, except for the first observed state which cuts in).
	 */
	bool bPendingInstant = false;
};
