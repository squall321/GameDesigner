// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsCamera module.
 *
 * These are ROOT/anchor + the shipped concrete-mode tags. Anchoring the roots guarantees the
 * hierarchy exists at startup so tag-hierarchy matching always works and designers can author
 * additional child mode tags in the project tag config.
 *
 * - Cam.Mode.*      : identity tags for camera modes (mapped to UCam_CameraMode classes by settings).
 * - Cam.Service     : service-locator key under which a director publishes its ICam_CameraModeProvider.
 * - Cam.InputMode   : input-mode tag pushed via the shared input arbiter while a mode requests it
 *                     (e.g. an orbit/photo mode that captures look input).
 */
namespace Cam_NativeTags
{
	// Camera mode identity root + shipped concrete modes.
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode);
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_ThirdPerson);
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_FirstPerson);
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_TopDown);
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_Orbit);
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_Fixed);

	// Service-locator key root for the camera-mode provider seam.
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ModeProvider);

	// Input-mode tag pushed through the shared input arbiter for look-capturing modes.
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode);

	// ---- Targeting / lock-on (shake library + targeting area) ----

	/** Service-locator key under which the local targeting component publishes its ICam_TargetSource. */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_TargetSource);

	/** Input-mode tag pushed via the shared arbiter while hard lock-on is active (remaps look to cycle). */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_LockOn);

	/** Bus channel fired (cosmetically) when the locked target changes; payload FCam_TargetChangedEvent. */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TargetChanged);

	// ---- Additive deepening: rail / cinematic / photo / post-process / advanced shake ----

	/** Identity tag for the spline/rail follow mode (mapped to UCam_SplineRailMode by settings). */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_SplineRail);

	/** Identity tag for the cinematic/framing mode (mapped to UCam_CinematicMode by settings). */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_Cinematic);

	/** Identity tag for the free-fly photo-mode camera (mapped to UCam_PhotoFreeFlyMode by settings). */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_PhotoFreeFly);

	/** Identity tag used by the director's transient cinematic-override entry on its own stack. */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mode_CinematicOverride);

	/** Input-mode tag pushed via the shared arbiter while photo mode is active (frees the cursor / free-fly). */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_PhotoMode);

	/** Service-locator key under which the local camera director publishes its ISeam_CinematicCameraSink. */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_CinematicSink);

	/**
	 * Bus channel fired (cosmetically) when the advanced-shake director plays a shake from an epicenter;
	 * payload FCam_ShakeEpicenterEvent. Lets project-side systems react to a shake hit without coupling.
	 */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_ShakeEpicenter);

	/**
	 * Bus channel the collision probe optionally broadcasts when the view target is occluded; payload
	 * FCam_OcclusionEvent (OcclusionAlpha + occluded target's FSeam_EntityId). Project-side material fade
	 * (dither/translucency) listens here; the probe itself never fades meshes.
	 */
	DESIGNPATTERNSCAMERA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_CameraOcclusion);
}
