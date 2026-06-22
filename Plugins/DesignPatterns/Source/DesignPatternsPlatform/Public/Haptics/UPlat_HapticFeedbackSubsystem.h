// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Platform/Seam_HapticController.h"
#include "GameplayTagContainer.h"
#include "UPlat_HapticFeedbackSubsystem.generated.h"

class UPlat_HapticEffectSet;
class UPlat_DeviceCapabilitySubsystem;
class APlayerController;
struct FPlat_HapticEffect;

/** Broadcast when the player toggles haptics on/off so UI can reflect the state. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnHapticsEnabledChanged, bool, bEnabled);

/**
 * Plays tag-keyed haptics by WRAPPING engine systems rather than reinventing them:
 *  - Gamepad force-feedback via APlayerController::ClientPlayForceFeedback / PlayDynamicForceFeedback.
 *  - Mobile vibration via FPlatformMisc (confined behind PLATFORM_* with a generic no-op fallback).
 *
 * Data-driven: every effect is a FPlat_HapticEffect row in a UPlat_HapticEffectSet bank resolved by
 * tag; intensities/durations are authored, not hard-coded. Gated by the device capabilities
 * (SupportsGamepadRumble / SupportsTouch) and a user/settings on-off toggle.
 *
 * Implements ISeam_HapticController and self-registers under DP.Service.Platform.Haptics (WeakObserved)
 * so gameplay/UI/combat trigger haptics through the seam without depending on this module. Skipped on
 * dedicated servers (no input device, no point). Cosmetic + LOCAL — never replicated.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_HapticFeedbackSubsystem : public UDP_GameInstanceSubsystem, public ISeam_HapticController
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast when haptics are toggled on/off. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Haptics")
	FPlat_OnHapticsEnabledChanged OnHapticsEnabledChanged;

	/**
	 * Play the haptic mapped to EffectTag on the local player's device. No-op when disabled, the device
	 * cannot deliver the effect, or the tag is unmapped.
	 * @param Scale  Caller intensity multiplier (combined with row/category/master scalars).
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Haptics")
	void PlayHaptic(FGameplayTag EffectTag, float Scale = 1.f);

	/** Stop all currently-playing force-feedback (and any pending motor-drive timer). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Haptics")
	void StopAllHaptics();

	/** Turn haptics on/off at runtime; broadcasts OnHapticsEnabledChanged when the state changes. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Haptics")
	void SetHapticsEnabled(bool bEnabled);

	/** True when haptics are enabled (settings toggle) AND the device can deliver at least one channel. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Haptics")
	bool AreHapticsEnabled() const;

	/** Swap the active effect bank (e.g. a genre-specific override). Null reverts to the settings default. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Haptics")
	void SetActiveEffectSet(UPlat_HapticEffectSet* Set);

	//~ Begin ISeam_HapticController
	virtual void PlayHapticByTag_Implementation(FGameplayTag EffectTag, float Scale) override;
	virtual void StopHaptics_Implementation() override;
	virtual bool AreHapticsEnabled_Implementation() const override;
	//~ End ISeam_HapticController

private:
	/** Resolve the local player controller (split-screen: index 0) that owns force-feedback. */
	APlayerController* ResolveLocalController() const;

	/** Play a force-feedback asset row through the local controller. */
	void PlayGamepadFF(const FPlat_HapticEffect& Effect, float Scale);

	/** Drive the motors / mobile vibration for a MotorValues row (platform-confined). */
	void PlayMobileVibration(const FPlat_HapticEffect& Effect, float Scale);

	/** Compute the final intensity scalar for a row: caller Scale * category scalar * master intensity. */
	float ComputeEffectiveScale(const FPlat_HapticEffect& Effect, float CallerScale) const;

	/** Register/unregister this subsystem as the haptic-controller service. */
	void RegisterHapticService();
	void UnregisterHapticService();

	/** Weak: the device-capability subsystem is engine-owned; we never keep it alive. */
	TWeakObjectPtr<UPlat_DeviceCapabilitySubsystem> CapsWeak;

	/** The currently-active effect bank (strong: the subsystem keeps its bank loaded while live). */
	UPROPERTY(Transient)
	TObjectPtr<UPlat_HapticEffectSet> ActiveSet = nullptr;

	/** Live enabled flag, seeded from settings and toggled by SetHapticsEnabled. */
	bool bHapticsEnabled = true;

	/** Tags of force-feedback effects we have started, so StopAllHaptics can stop them by tag. */
	TSet<FName> ActiveTags;

	/** True once we successfully registered the service (so we only unregister our own binding). */
	bool bRegisteredService = false;
};
