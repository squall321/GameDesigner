// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Cam_CameraModeProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UCam_CameraModeProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam over "something that owns a priority/blend camera-mode stack and can be asked to
 * push/pop modes by tag". The concrete implementation in this module is
 * UCam_CameraDirectorComponent (a component on the locally-controlled pawn/controller),
 * but consumers — a Narrative cutscene that wants a fixed cinematic angle, a HUD photo-mode,
 * an Interaction lock-on — depend ONLY on this interface so they never hard-include the
 * Camera module's concrete director.
 *
 * Resolution: callers resolve the provider off the locally-controlled actor
 * (Controller / Pawn -> FindComponentByInterface) or via the service locator under the
 * camera provider service tag (see Cam_NativeTags). All operations are LOCAL and COSMETIC —
 * the camera stack is never replicated; it is driven by already-replicated gameplay state
 * (via OnRep / the message bus) on each client.
 *
 * Push/pop is priority-keyed and request-id-keyed, mirroring the shared input-mode arbiter
 * seam: the highest-priority active push wins; popping a request restores the next-highest;
 * a caller pops exactly its own request by id without disturbing other pushers.
 */
class DESIGNPATTERNSCAMERA_API ICam_CameraModeProvider
{
	GENERATED_BODY()

public:
	/**
	 * Push a camera mode (identified by ModeTag, resolved to an instanced UCam_CameraMode by
	 * the director's settings/registry) onto the stack at the given priority.
	 *
	 * @param ModeTag   Stable mode identity tag (anchor under Cam.Mode.*). Must be valid.
	 * @param Priority  Higher wins. Ties are broken by push order (later push is on top).
	 * @return An opaque request id; pass it to PopCameraMode to release exactly this push.
	 *         Returns an invalid FGuid if ModeTag is invalid or no mode could be resolved.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Provider")
	FGuid PushCameraMode(FGameplayTag ModeTag, int32 Priority);

	/** Release a previously-pushed mode. Safe to call with an invalid/already-popped id (no-op). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Provider")
	void PopCameraMode(FGuid RequestId);

	/** The tag of the currently-winning (top, fully-blended-toward) mode; empty if the stack is idle. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Provider")
	FGameplayTag GetActiveModeTag() const;
};
