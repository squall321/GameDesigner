// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Pause/Flow_PauseController.h"
#include "Flow/Flow_GameFlowSubsystem.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/World.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_PauseController::Initialize(UFlow_GameFlowSubsystem* InOwner)
{
	Owner = InOwner;

	// Register as an app-lifecycle listener if a Platform adapter is present.
	if (UObject* Lifecycle = ResolveLifecycle())
	{
		TScriptInterface<ISeam_LifecycleListener> Self;
		Self.SetObject(this);
		Self.SetInterface(Cast<ISeam_LifecycleListener>(this));
		ISeam_AppLifecycle::Execute_RegisterLifecycleListener(Lifecycle, Self);
		bRegisteredListener = true;
		UE_LOG(LogDP, Log, TEXT("[Flow][Pause] Registered as ISeam_LifecycleListener."));
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("[Flow][Pause] No ISeam_AppLifecycle adapter; auto-pause inert."));
	}
}

void UFlow_PauseController::Shutdown()
{
	if (bRegisteredListener)
	{
		if (UObject* Lifecycle = ResolveLifecycle())
		{
			TScriptInterface<ISeam_LifecycleListener> Self;
			Self.SetObject(this);
			Self.SetInterface(Cast<ISeam_LifecycleListener>(this));
			ISeam_AppLifecycle::Execute_UnregisterLifecycleListener(Lifecycle, Self);
		}
		bRegisteredListener = false;
	}
	CachedLifecycle.Reset();
	Owner.Reset();
}

// ---------------------------------------------------------------------------------------------------
// Lifecycle callbacks
// ---------------------------------------------------------------------------------------------------

void UFlow_PauseController::OnAppSuspended_Implementation()
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	if (Settings && !Settings->bAutoPauseOnFocusLoss)
	{
		return; // Auto-pause disabled by config.
	}
	DoPause(/*bFromFocusLoss*/ true);
}

void UFlow_PauseController::OnAppResumed_Implementation()
{
	// Only auto-resume a pause WE drove via focus loss; a manually-opened pause menu is left to the player.
	if (bPausedByFocusLoss)
	{
		RequestResume();
	}
}

// ---------------------------------------------------------------------------------------------------
// Pause / resume
// ---------------------------------------------------------------------------------------------------

void UFlow_PauseController::RequestPause()
{
	DoPause(/*bFromFocusLoss*/ false);
}

void UFlow_PauseController::DoPause(bool bFromFocusLoss)
{
	UFlow_GameFlowSubsystem* Flow = Owner.Get();
	if (!Flow)
	{
		return;
	}

	// Already paused: nothing to do.
	if (Flow->IsPaused())
	{
		return;
	}

	// Only pause from active gameplay (don't pause a menu / loading screen).
	if (!Flow->GetCurrentPhase().MatchesTag(FlowTags::Phase_InGame))
	{
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const bool bAllowMPPause = Settings && Settings->bAllowPauseInMultiplayer;

	if (IsStandalone() || bAllowMPPause)
	{
		// Single-player (or a project that opts into MP pause): the existing Pause phase definition's
		// bPausesGame side effect engine-pauses. We use the validated transition (Pause must be an allowed
		// edge / undeclared-open) so guards still apply.
		if (Flow->RequestTransition_Implementation(FlowTags::Phase_Pause))
		{
			bPausedByFocusLoss = bFromFocusLoss;
		}
	}
	else
	{
		// Multiplayer: never engine-pause a shared world. Push the pause overlay (via the Pause phase, whose
		// definition in MP should have bPausesGame=false) and emit an autosave hint for a save UI/GameMode.
		if (Flow->RequestTransition_Implementation(FlowTags::Phase_Pause))
		{
			bPausedByFocusLoss = bFromFocusLoss;
		}
		if (UDP_MessageBusSubsystem* Bus = GetBus())
		{
			Bus->BroadcastPayload(FlowTags::Bus_AutoSaveHint, FInstancedStruct(), this);
		}
		UE_LOG(LogDP, Log, TEXT("[Flow][Pause] MP focus loss: pause overlay + autosave hint (no engine pause)."));
	}
}

void UFlow_PauseController::RequestResume()
{
	UFlow_GameFlowSubsystem* Flow = Owner.Get();
	if (!Flow || !Flow->IsPaused())
	{
		bPausedByFocusLoss = false;
		return;
	}

	// Pop back to the phase below Pause on the flow back-stack.
	Flow->GoBack();
	bPausedByFocusLoss = false;
}

// ---------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------

bool UFlow_PauseController::IsStandalone() const
{
	const UFlow_GameFlowSubsystem* Flow = Owner.Get();
	const UWorld* World = Flow ? Flow->GetWorld() : nullptr;
	return World && World->GetNetMode() == NM_Standalone;
}

UDP_MessageBusSubsystem* UFlow_PauseController::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

UObject* UFlow_PauseController::ResolveLifecycle() const
{
	if (CachedLifecycle.IsValid())
	{
		return CachedLifecycle.GetObject();
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Obj = Locator->ResolveService(FlowTags::Service_AppLifecycle);
	if (Obj && Obj->Implements<USeam_AppLifecycle>())
	{
		if (ISeam_AppLifecycle* AsInterface = Cast<ISeam_AppLifecycle>(Obj))
		{
			const_cast<UFlow_PauseController*>(this)->CachedLifecycle = TWeakInterfacePtr<ISeam_AppLifecycle>(*AsInterface);
		}
		return Obj;
	}
	return nullptr;
}
