// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lifecycle/UPlat_AppLifecycleSubsystem.h"
#include "Core/DPLog.h"
#include "Misc/CoreDelegates.h"

void UPlat_AppLifecycleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to the engine application lifecycle delegates. These fire on mobile suspend/
	// resume and console constrained/unconstrained; on desktop the deactivate/reactivate pair
	// fires on focus loss/gain, giving a portable auto-pause signal everywhere.
	WillEnterBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(
		this, &UPlat_AppLifecycleSubsystem::HandleWillEnterBackground);
	WillDeactivateHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(
		this, &UPlat_AppLifecycleSubsystem::HandleWillDeactivate);
	EnteredForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
		this, &UPlat_AppLifecycleSubsystem::HandleEnteredForeground);
	ReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(
		this, &UPlat_AppLifecycleSubsystem::HandleReactivated);

	UE_LOG(LogDP, Log, TEXT("[Plat] AppLifecycle subscribed to FCoreDelegates."));
}

void UPlat_AppLifecycleSubsystem::Deinitialize()
{
	// Unsubscribe EVERY FCoreDelegates handle we registered.
	if (WillEnterBackgroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(WillEnterBackgroundHandle);
		WillEnterBackgroundHandle.Reset();
	}
	if (WillDeactivateHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(WillDeactivateHandle);
		WillDeactivateHandle.Reset();
	}
	if (EnteredForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(EnteredForegroundHandle);
		EnteredForegroundHandle.Reset();
	}
	if (ReactivatedHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(ReactivatedHandle);
		ReactivatedHandle.Reset();
	}

	OnAppSuspended.Clear();
	OnAppResumed.Clear();

	UE_LOG(LogDP, Log, TEXT("[Plat] AppLifecycle unsubscribed from FCoreDelegates."));
	Super::Deinitialize();
}

FString UPlat_AppLifecycleSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("AppLifecycle: Suspended=%d"), bIsSuspended ? 1 : 0);
}

void UPlat_AppLifecycleSubsystem::HandleWillEnterBackground()
{
	EnterSuspended();
}

void UPlat_AppLifecycleSubsystem::HandleWillDeactivate()
{
	EnterSuspended();
}

void UPlat_AppLifecycleSubsystem::HandleEnteredForeground()
{
	ExitSuspended();
}

void UPlat_AppLifecycleSubsystem::HandleReactivated()
{
	ExitSuspended();
}

void UPlat_AppLifecycleSubsystem::EnterSuspended()
{
	if (bIsSuspended)
	{
		return; // debounce duplicate background/deactivate events
	}
	bIsSuspended = true;
	UE_LOG(LogDP, Log, TEXT("[Plat] App suspended."));
	OnAppSuspended.Broadcast();
}

void UPlat_AppLifecycleSubsystem::ExitSuspended()
{
	if (!bIsSuspended)
	{
		return; // debounce duplicate foreground/reactivate events
	}
	bIsSuspended = false;
	UE_LOG(LogDP, Log, TEXT("[Plat] App resumed."));
	OnAppResumed.Broadcast();
}
