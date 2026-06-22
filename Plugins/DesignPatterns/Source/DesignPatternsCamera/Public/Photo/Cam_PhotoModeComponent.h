// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Mode/Cam_PhotoFreeFlyMode.h"
#include "Cam_PhotoModeComponent.generated.h"

class ICam_CameraModeProvider;
class ISeam_InputModeArbiter;
class ISeam_SafeZoneProvider;

/** Broadcast when photo mode is entered/exited so local UI (guides, controls) can show/hide. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCam_OnPhotoModeChanged, bool, bIsActive);

/**
 * Player-owned component that drives free-fly photo mode. LOCAL and COSMETIC — never replicated.
 *
 * EnterPhotoMode:
 *   - resolves an ICam_CameraModeProvider (the owner's director via FindComponentByInterface, else the
 *     service locator) and pushes Cam.Mode.PhotoFreeFly at PhotoModePriority,
 *   - resolves the shared ISeam_InputModeArbiter from the locator and pushes Cam.InputMode.PhotoMode so
 *     the Platform input router frees the cursor / routes free-fly input (never reinvented here),
 *   - suppresses gameplay shakes on a sibling UCam_ShakeRequestComponent so the photo is steady.
 *
 * Each tick it forwards accumulated AddMove/Look/Roll/FOV input into the live UCam_PhotoFreeFlyMode by
 * reaching it through the director's public GetStack()->GetTopMode() + Cast (the mode is guaranteed top
 * at the photo priority while active). ExitPhotoMode pops both requests by id and restores shakes.
 *
 * It reads the title-safe insets through ISeam_SafeZoneProvider (resolved from the locator) and exposes
 * them so photo-mode UI can draw rule-of-thirds / framing guides inside the safe area.
 *
 * No-ops entirely when the owner is not locally controlled (dedicated server / remote proxy).
 */
UCLASS(ClassGroup = "DesignPatterns", meta = (BlueprintSpawnableComponent), Blueprintable,
	HideCategories = ("Collision", "Cooking", "AssetUserData", "Replication"))
class DESIGNPATTERNSCAMERA_API UCam_PhotoModeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCam_PhotoModeComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Enter free-fly photo mode (push mode + input mode, suppress shakes). No-op if already active. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void EnterPhotoMode();

	/** Exit photo mode (pop mode + input mode, restore shakes). No-op if not active. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void ExitPhotoMode();

	/** Toggle photo mode on/off. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void TogglePhotoMode();

	/** True while photo mode is active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Photo")
	bool IsPhotoModeActive() const { return bActive; }

	/** Accumulate local-space move input (X fwd, Y right, Z up) for this frame. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void AddMoveInput(FVector LocalDelta);

	/** Accumulate look input (pitch/yaw deg) for this frame. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void AddLookInput(FRotator LookDelta);

	/** Accumulate roll input (normalised axis, scaled by mode RollSpeed) for this frame. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void AddRollInput(float RollAxis);

	/** Accumulate FOV input (deg, negative zooms in) for this frame. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Photo")
	void AddFOVInput(float FOVDelta);

	/**
	 * Title-safe insets (Left, Top, Right, Bottom in pixels) for drawing framing guides, read from the
	 * shared ISeam_SafeZoneProvider. Returns zero when no provider is registered.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Photo")
	FVector4 GetSafeInsets() const;

	/** Fired when photo mode is entered/exited. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Camera|Photo")
	FCam_OnPhotoModeChanged OnPhotoModeChanged;

protected:
	/** Priority for the pushed photo camera mode. High so it wins over gameplay modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo")
	int32 PhotoModePriority = 1000;

	/** Priority for the pushed photo input mode through the shared arbiter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo")
	int32 PhotoInputPriority = 200;

	/** When true, sibling shake components are suppressed while photo mode is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo")
	bool bSuppressShakes = true;

private:
	/** Resolve the camera-mode provider off the owner (interface) or the service locator. */
	TScriptInterface<ICam_CameraModeProvider> ResolveModeProvider() const;

	/** Resolve the shared input-mode arbiter from the service locator, or null. */
	TScriptInterface<ISeam_InputModeArbiter> ResolveInputArbiter() const;

	/** Resolve the shared safe-zone provider from the service locator, or null. */
	TScriptInterface<ISeam_SafeZoneProvider> ResolveSafeZoneProvider() const;

	/** Feed Accumulated into the live photo mode via the provider's stack top, then clear it. */
	void FeedInputToLiveMode();

	/** Toggle sibling shake components on/off (suppression). */
	void SetShakesSuppressed(bool bSuppressed);

	/** Resolve the owning local player controller, or null. */
	class APlayerController* ResolveOwningController() const;

	/** Whether photo mode is currently active. */
	UPROPERTY(Transient)
	bool bActive = false;

	/** Request id of the pushed camera mode (invalid when inactive). */
	UPROPERTY(Transient)
	FGuid ModeRequestId;

	/** Request id of the pushed input mode (invalid when inactive). */
	UPROPERTY(Transient)
	FGuid InputRequestId;

	/** Cached mode provider (re-resolved if stale). */
	UPROPERTY(Transient)
	TScriptInterface<ICam_CameraModeProvider> ModeProviderCache;

	/** Accumulated input for the current frame, flushed into the live mode each tick. */
	UPROPERTY(Transient)
	FCam_PhotoInput Accumulated;
};
