// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Platform/Seam_AppLifecycle.h"
#include "UObject/ScriptInterface.h"
#include "UPlat_AppLifecycleAdapter.generated.h"

class UPlat_AppLifecycleSubsystem;

/**
 * GameInstance-lifetime adapter that exposes the shipped UPlat_AppLifecycleSubsystem through the shared
 * ISeam_AppLifecycle seam so GameFlow's pause controller can register for suspend/resume WITHOUT
 * depending on the Platform module. Created and owned by UPlat_AppLifecycleServiceSubsystem, which
 * registers it StrongOwned under DP.Service.Platform.AppLifecycle.
 *
 * It binds to the existing subsystem's Blueprint delegates (OnAppSuspended/OnAppResumed) and fans them
 * out to its registered listeners, which it holds WEAKLY (pruned on each event) so a dead world's
 * listener can never be kept alive or fired into.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_AppLifecycleAdapter : public UObject, public ISeam_AppLifecycle
{
	GENERATED_BODY()

public:
	/** Bind to the underlying lifecycle subsystem's delegates. Safe to call once after construction. */
	void BindToSubsystem(UPlat_AppLifecycleSubsystem* InSubsystem);

	/** Unbind from the underlying subsystem and drop all listeners (called on teardown). */
	void Shutdown();

	//~ Begin ISeam_AppLifecycle
	virtual bool IsSuspended_Implementation() const override;
	virtual void RegisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& Listener) override;
	virtual void UnregisterLifecycleListener_Implementation(const TScriptInterface<ISeam_LifecycleListener>& Listener) override;
	//~ End ISeam_AppLifecycle

private:
	/** Bound to UPlat_AppLifecycleSubsystem::OnAppSuspended; fans out to listeners. */
	UFUNCTION()
	void HandleSuspended();

	/** Bound to UPlat_AppLifecycleSubsystem::OnAppResumed; fans out to listeners. */
	UFUNCTION()
	void HandleResumed();

	/** Drop any listeners whose object has been GC'd. */
	void PruneListeners();

	/** Weak: the underlying subsystem is GI-owned; we never keep it alive. */
	TWeakObjectPtr<UPlat_AppLifecycleSubsystem> SubsystemWeak;

	/** Listeners held weakly via their UObject so a dead listener cannot be kept alive or fired into. */
	TArray<TWeakObjectPtr<UObject>> Listeners;
};
