// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * WorldSystems runtime module for the DesignPatterns plugin.
 *
 * Genre-agnostic, mostly COSMETIC ambient-world framework. This module provides:
 *   - A tag-keyed WEATHER state model (UWS_WeatherSubsystem) driven as a small state machine: the
 *     server picks a weather state by tag, the lightweight current-state replicates via a tiny AInfo
 *     carrier (subsystems never replicate), and the local cosmetic response (particles + ambient
 *     sound) is dispatched by tag through the shared VFX controller seam and the message bus. The
 *     weather model reacts to the shared simulation clock (ISeam_SimClock) for time-of-day blends.
 *   - A tag-keyed cosmetic VFX MANAGER (UWS_VfxManagerSubsystem) that implements ISeam_VfxController,
 *     resolves Niagara/Cascade systems from data-driven VFX banks, and recycles their carrier actors
 *     through the core object pool. VFX is purely local and is NEVER replicated.
 *
 * The module depends only on the core "DesignPatterns" module and the shared "DesignPatternsSeams"
 * contracts. It never hard-includes another genre or sibling Wave-3 module: all cross-module coupling
 * goes through seams (TScriptInterface resolved from the service locator / off the owning actor) and
 * the message bus. To avoid a hard dependency on the Niagara module the VFX manager works against the
 * engine UFXSystemAsset / UFXSystemComponent base types and the engine spawn helpers, which transparently
 * handle both Cascade and Niagara.
 */
class FDesignPatternsWorldSystemsModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsWorldSystems module.
 *
 * These are ROOT/anchor tags plus the small set of stable leaf keys this module itself needs (its
 * service-locator keys and its bus channels). Concrete WEATHER STATE ids and VFX ids are authored by
 * the game project as CHILD tags under the roots below (in the project's tag config or its own native
 * tags); anchoring the roots here guarantees the hierarchy exists at startup so tag-hierarchy matching
 * always works.
 *
 * Tag layout (all under the shared "WS" root for this module, plus shared DP.Service / DP.Bus roots):
 *   WS                          - module umbrella root.
 *   WS.Weather                  - weather subtree root.
 *   WS.Weather.State.*          - per-weather-state identity keys (Clear / Rain / Storm / Fog / Snow ...).
 *                                 PROJECT-AUTHORED leaves, resolved against WS_WeatherStateDataAsset.
 *   WS.Vfx                      - VFX subtree root.
 *   WS.Vfx.*                    - per-VFX identity keys (resolved against WS_VfxBankDataAsset).
 *                                 PROJECT-AUTHORED leaves.
 *   DP.Service.Weather          - service-locator key the weather subsystem registers itself under.
 *   DP.Service.Vfx              - service-locator key the VFX manager registers itself under
 *                                 (the ISeam_VfxController provider).
 *   DP.Bus.WorldSystems         - message-bus root for this module.
 *   DP.Bus.WorldSystems.WeatherChanged - broadcast when the active weather state changes
 *                                 (payload: FWS_WeatherChangedMessage).
 *   DP.Bus.WorldSystems.RequestWeather - broadcast to request a weather change without resolving the
 *                                 weather subsystem directly (payload: FWS_RequestWeatherMessage).
 */
namespace WorldSystemsNativeTags
{
	/** Module umbrella root: WS. Anchors the whole WorldSystems tag hierarchy. */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WS);

	/** Weather subtree root: WS.Weather. */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather);

	/** Root for per-weather-state identity keys (WS.Weather.State.Rain ...). PROJECT authors the leaves. */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Weather_State);

	/** VFX subtree root: WS.Vfx. PROJECT authors per-effect leaves under this. */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Vfx);

	/** Service-locator key the weather subsystem registers itself under (child of DP.Service). */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Weather);

	/** Service-locator key the VFX manager (ISeam_VfxController provider) registers itself under. */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Vfx);

	/** Root for message-bus channels this module participates in (children of DP.Bus). */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	/**
	 * Bus channel the weather subsystem broadcasts on whenever the active weather state changes
	 * (server and clients, after replication). Payload: FWS_WeatherChangedMessage. Audio / lighting
	 * systems listen here to react without depending on the weather subsystem's concrete type.
	 */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WeatherChanged);

	/**
	 * Bus channel a game broadcasts to request a weather change by tag without resolving the weather
	 * subsystem directly (payload: FWS_RequestWeatherMessage). The weather subsystem listens here and,
	 * on authority, applies the requested state; clients ignore it.
	 */
	DESIGNPATTERNSWORLDSYSTEMS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RequestWeather);
}
