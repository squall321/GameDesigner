// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakInterfacePtr.h"
#include "Platform/Seam_AppLifecycle.h"
#include "Flow_PauseController.generated.h"

class UFlow_GameFlowSubsystem;
class UDP_MessageBusSubsystem;

/**
 * Pause / focus-loss orchestration on behalf of the flow subsystem. Owned as a UPROPERTY(Transient)
 * subobject of UFlow_GameFlowSubsystem (NewObject(Outer)); GameInstance-lifetime so it survives travel.
 *
 * It implements ISeam_LifecycleListener and registers itself with the shared ISeam_AppLifecycle adapter
 * (resolved through the locator). On OS suspend / focus loss it auto-pauses:
 *  - STANDALONE (single-player): requests a transition to Flow.Phase.Pause, reusing the EXISTING per-phase
 *    bPausesGame side effect to actually pause the engine.
 *  - MULTIPLAYER: never engine-pauses a shared world. Instead it pushes the pause overlay screen and emits
 *    an autosave HINT on the bus (DP.Bus.Flow.AutoSaveHint) for a save UI / GameMode to honour.
 * On resume it pops back to the prior phase via the flow back-stack (GoBack).
 *
 * Honours the settings bAutoPauseOnFocusLoss / bAllowPauseInMultiplayer. Degrades inert when no
 * ISeam_AppLifecycle adapter is registered (manual RequestPause/Resume still work).
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_PauseController : public UObject, public ISeam_LifecycleListener
{
	GENERATED_BODY()

public:
	/** Bind to the owning flow subsystem and register as an app-lifecycle listener. */
	void Initialize(UFlow_GameFlowSubsystem* InOwner);

	/** Unregister the lifecycle listener (called from the owner's Deinitialize). */
	void Shutdown();

	//~ Begin ISeam_LifecycleListener
	/** OS suspend / focus loss: auto-pause per net mode + settings. */
	virtual void OnAppSuspended_Implementation() override;
	/** OS resume / focus regain: pop back to the prior phase. */
	virtual void OnAppResumed_Implementation() override;
	//~ End ISeam_LifecycleListener

	/** Manually request a pause (same logic as a focus-loss suspend, ignoring bAutoPauseOnFocusLoss). */
	UFUNCTION(BlueprintCallable, Category = "Flow|Pause")
	void RequestPause();

	/** Manually request a resume (pop back from Pause). */
	UFUNCTION(BlueprintCallable, Category = "Flow|Pause")
	void RequestResume();

private:
	/** True if the current world is a standalone (single-player) session that may engine-pause. */
	bool IsStandalone() const;

	/** Resolve the owning GameInstance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Resolve the ISeam_AppLifecycle adapter object, or null. */
	UObject* ResolveLifecycle() const;

	/** Shared pause path used by both the listener and the manual request. */
	void DoPause(bool bFromFocusLoss);

	// --- State ---

	/** Owning flow subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlow_GameFlowSubsystem> Owner;

	/** Weakly-held resolved app-lifecycle seam (re-resolved when stale). */
	TWeakInterfacePtr<ISeam_AppLifecycle> CachedLifecycle;

	/** True if WE drove the current pause (so resume only pops a pause we caused). */
	bool bPausedByFocusLoss = false;

	/** True once we have registered ourselves as a lifecycle listener. */
	bool bRegisteredListener = false;
};
