// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Input/UPlat_InputRouterSubsystem.h"
#include "Core/DPLog.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformMisc.h"

void UPlat_InputRouterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// All platform branching is confined here. PLATFORM_* macros are always defined (0/1),
	// so this is compile-safe on every target with a generic-desktop fallback.
#if PLATFORM_ANDROID || PLATFORM_IOS
	bIsTouchPlatform = true;
#else
	bIsTouchPlatform = false;
#endif

	CurrentDevice = ResolveInitialDevice();

	UE_LOG(LogDP, Log, TEXT("[Plat] InputRouter initialized. TouchPlatform=%d StartDevice=%d"),
		bIsTouchPlatform ? 1 : 0, static_cast<int32>(CurrentDevice));
}

void UPlat_InputRouterSubsystem::Deinitialize()
{
	OnInputDeviceChanged.Clear();
	Super::Deinitialize();
}

FString UPlat_InputRouterSubsystem::GetDPDebugString_Implementation() const
{
	const TCHAR* DeviceName = TEXT("KeyboardMouse");
	switch (CurrentDevice)
	{
	case EPlat_InputDevice::Gamepad: DeviceName = TEXT("Gamepad"); break;
	case EPlat_InputDevice::Touch:   DeviceName = TEXT("Touch");   break;
	default: break;
	}
	return FString::Printf(TEXT("InputRouter: Device=%s TouchPlatform=%d"),
		DeviceName, bIsTouchPlatform ? 1 : 0);
}

bool UPlat_InputRouterSubsystem::IsTouchPlatform() const
{
	return bIsTouchPlatform || CurrentDevice == EPlat_InputDevice::Touch;
}

EPlat_InputDevice UPlat_InputRouterSubsystem::ResolveInitialDevice()
{
#if PLATFORM_ANDROID || PLATFORM_IOS
	return EPlat_InputDevice::Touch;
#elif PLATFORM_CONSOLE
	return EPlat_InputDevice::Gamepad;
#else
	// Generic desktop (Win/Mac/Linux) fallback. Default to keyboard & mouse; the input layer
	// flips this to Gamepad live via NotifyInputFromDevice the first time a pad reports input.
	return EPlat_InputDevice::KeyboardMouse;
#endif
}

void UPlat_InputRouterSubsystem::NotifyInputFromDevice(EPlat_InputDevice Device)
{
	const EPlat_InputDevice Classified = OnInputDeviceClassified(Device);
	if (Classified == CurrentDevice)
	{
		return;
	}

	CurrentDevice = Classified;
	UE_LOG(LogDP, Verbose, TEXT("[Plat] Input device changed to %d"), static_cast<int32>(CurrentDevice));
	OnInputDeviceChanged.Broadcast(CurrentDevice);
}

EPlat_InputDevice UPlat_InputRouterSubsystem::OnInputDeviceClassified_Implementation(EPlat_InputDevice ReportedDevice) const
{
	return ReportedDevice;
}
