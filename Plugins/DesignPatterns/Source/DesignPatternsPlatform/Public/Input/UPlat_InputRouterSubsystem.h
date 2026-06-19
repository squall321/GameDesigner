// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UPlat_InputRouterSubsystem.generated.h"

/**
 * The coarse class of input device currently driving the game.
 *
 * Intentionally minimal: most gameplay/UI code only needs to know whether the player is on a
 * pointer-and-keys device, a gamepad, or a touch screen so it can swap prompts and layouts.
 */
UENUM(BlueprintType)
enum class EPlat_InputDevice : uint8
{
	/** Mouse + keyboard (typical desktop). */
	KeyboardMouse	UMETA(DisplayName = "Keyboard & Mouse"),
	/** Any connected/active gamepad (desktop, console). */
	Gamepad			UMETA(DisplayName = "Gamepad"),
	/** Touch screen (mobile / handheld). */
	Touch			UMETA(DisplayName = "Touch")
};

/** Broadcast whenever the active input device class changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnInputDeviceChanged, EPlat_InputDevice, NewDevice);

/**
 * Abstracts the *source* of input so the rest of the plugin stays platform-agnostic.
 *
 * Detection rules (all platform #ifdefs live here):
 *  - Touch platforms (Android/iOS) default to Touch.
 *  - Desktop/console default to KeyboardMouse/Gamepad and flip live as the player switches.
 *  - Games can also push the device explicitly (e.g. from an Enhanced Input device-type hook)
 *    via NotifyInputFromDevice, which is the recommended live path.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_InputRouterSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Fired when the active input device class changes (e.g. player picks up a gamepad). */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Input")
	FPlat_OnInputDeviceChanged OnInputDeviceChanged;

	/** The input device class currently believed to be active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Input")
	EPlat_InputDevice GetCurrentInputDevice() const { return CurrentDevice; }

	/** True on touch-first platforms (Android/iOS), or when Touch is the active device. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Input")
	bool IsTouchPlatform() const;

	/** True when a gamepad is the active device. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Input")
	bool IsGamepadActive() const { return CurrentDevice == EPlat_InputDevice::Gamepad; }

	/**
	 * Push the live input device class. Call this from your input layer (e.g. an Enhanced Input
	 * device-type callback). Only broadcasts when the class actually changes. Designer hook
	 * OnInputDeviceClassified runs first so projects can veto/remap.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Input")
	void NotifyInputFromDevice(EPlat_InputDevice Device);

	/**
	 * Designer override: classify or remap a reported device before it becomes current.
	 * Default returns the reported device unchanged.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Platform|Input")
	EPlat_InputDevice OnInputDeviceClassified(EPlat_InputDevice ReportedDevice) const;
	virtual EPlat_InputDevice OnInputDeviceClassified_Implementation(EPlat_InputDevice ReportedDevice) const;

private:
	/** Compute the most likely starting device from platform capabilities. */
	static EPlat_InputDevice ResolveInitialDevice();

	/** Cached touch-platform flag (platform-static, computed once). */
	bool bIsTouchPlatform = false;

	/** Current active device class. */
	UPROPERTY()
	EPlat_InputDevice CurrentDevice = EPlat_InputDevice::KeyboardMouse;
};
