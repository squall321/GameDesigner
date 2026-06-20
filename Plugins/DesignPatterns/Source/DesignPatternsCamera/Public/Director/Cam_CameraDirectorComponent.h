// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/Cam_CameraModeProvider.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_CameraDirectorComponent.generated.h"

class UCam_CameraModeStack;
class UCam_CameraModifier;
class APlayerController;
class APlayerCameraManager;

/**
 * Player-owned camera director. Lives on the locally-controlled pawn or player controller and is the
 * single owner of the camera-mode stack for that local player. Implements ICam_CameraModeProvider so
 * any system can push/pop modes by tag without depending on this concrete type.
 *
 * Each tick (after the pawn has moved) it:
 *   1. Builds an FCam_ViewContext from the controlled actor (pivot, control rotation, previous view).
 *   2. Asks the stack to evaluate and blend the active modes into one FCam_CameraView.
 *   3. Pushes that view onto a UCam_CameraModifier installed on the local APlayerCameraManager.
 * It NEVER calls SetViewTarget or otherwise fights the manager — it only feeds the modifier.
 *
 * LOCAL/COSMETIC: the stack and the resulting view are never replicated. Mode changes are driven by
 * already-replicated gameplay (gameplay code or message-bus listeners call PushCameraMode on each
 * client). The component only runs on the owning local player (it early-outs on non-local controllers).
 *
 * Look-capturing modes (orbit/photo) request the shared input-mode arbiter seam so input routing is
 * handled by the Platform module rather than reinvented here.
 */
UCLASS(ClassGroup = "DesignPatterns", meta = (BlueprintSpawnableComponent), Blueprintable)
class DESIGNPATTERNSCAMERA_API UCam_CameraDirectorComponent : public UActorComponent, public ICam_CameraModeProvider
{
	GENERATED_BODY()

public:
	UCam_CameraDirectorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	//~ Begin ICam_CameraModeProvider
	virtual FGuid PushCameraMode_Implementation(FGameplayTag ModeTag, int32 Priority) override;
	virtual void PopCameraMode_Implementation(FGuid RequestId) override;
	virtual FGameplayTag GetActiveModeTag_Implementation() const override;
	//~ End ICam_CameraModeProvider

	/** The stack this director owns (may be null before BeginPlay). */
	UCam_CameraModeStack* GetStack() const { return ModeStack; }

protected:
	/**
	 * Socket-relative or actor-relative pivot offset applied to the view target's location to produce
	 * the pivot the modes orbit/frame around. Tunable; default raises the pivot to roughly head height
	 * for a humanoid. Projects override per-pawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FVector PivotOffset = FVector(0.f, 0.f, 60.f);

	/**
	 * Optional bone/socket on the view target's mesh used as the pivot instead of actor location +
	 * offset. Empty = use actor location + PivotOffset. Resolved against the first skeletal mesh.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	FName PivotSocketName = NAME_None;

	/** Priority pushed for the look-capture input mode when a look-capturing mode is on top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Input")
	int32 LookCaptureInputPriority = 100;

private:
	/** Resolve (and cache) the local player controller that owns this director, or null. */
	APlayerController* ResolveOwningController() const;

	/** Resolve the local APlayerCameraManager, or null. */
	APlayerCameraManager* ResolveCameraManager() const;

	/** Lazily create and install the modifier on the camera manager. Returns null if unavailable. */
	UCam_CameraModifier* EnsureModifier();

	/** Build this frame's read-only view context from the controlled actor. */
	FCam_ViewContext BuildViewContext() const;

	/** Instance the mode class mapped to ModeTag (Outer = the stack). Null if unmapped. */
	UCam_CameraMode* InstanceModeForTag(FGameplayTag ModeTag);

	/** Register/unregister this provider into the service locator (if settings enable it). */
	void RegisterAsProviderService();
	void UnregisterProviderService();

	/** Push/pop the look-capture input mode through the shared arbiter as the top mode changes. */
	void UpdateInputModeForTopMode();

	/** Resolve the shared input-mode arbiter seam from the service locator, or null. */
	TScriptInterface<class ISeam_InputModeArbiter> ResolveInputArbiter() const;

	/** The owned mode stack. Instanced subobject, GC-rooted via UPROPERTY. */
	UPROPERTY(Transient)
	TObjectPtr<UCam_CameraModeStack> ModeStack = nullptr;

	/** The modifier installed on the camera manager. Non-owning; the manager owns lifetime. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCam_CameraModifier> Modifier;

	/** Cached controller (resolved lazily; revalidated each use). */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> CachedController;

	/** The request id of the auto-pushed default mode, so we can pop it on teardown. */
	UPROPERTY(Transient)
	FGuid DefaultModeRequestId;

	/** The current input-mode request id held for a look-capturing top mode (invalid if none). */
	UPROPERTY(Transient)
	FGuid LookCaptureInputRequestId;

	/** The previously-applied view, fed back into the context for frame-to-frame easing. */
	UPROPERTY(Transient)
	FCam_CameraView LastAppliedView;

	/** Whether LastAppliedView has been seeded. */
	UPROPERTY(Transient)
	bool bHasLastView = false;

	/** True once we registered into the service locator (so EndPlay unregisters symmetrically). */
	UPROPERTY(Transient)
	bool bRegisteredService = false;
};
