// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_AppLifecycle.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_LifecycleListener : public UInterface
{
	GENERATED_BODY()
};

/**
 * Listener half of the app-lifecycle seam. An object that wants to react to OS-level suspend/resume
 * (focus loss, background, low-power) implements this and registers through ISeam_AppLifecycle. The
 * GameFlow pause controller implements it to auto-pause on suspend.
 *
 * Callbacks fire on the game thread. Implementations must be cheap and re-entrancy-safe (a single OS
 * event may fan out to many listeners).
 */
class DESIGNPATTERNSSEAMS_API ISeam_LifecycleListener
{
	GENERATED_BODY()

public:
	/** The application was suspended / lost focus / was backgrounded. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void OnAppSuspended();

	/** The application was resumed / regained focus / returned to the foreground. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void OnAppResumed();
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_AppLifecycle : public UInterface
{
	GENERATED_BODY()
};

/**
 * Subscribe + read seam over OS application lifecycle (suspend/resume, focus, background).
 *
 * The Platform module ships the concrete adapter (GameInstance-scoped, registered StrongOwned under
 * FlowTags::Service_AppLifecycle) that forwards the engine-backed FCoreDelegates application
 * suspend/resume / focus delegates to every registered ISeam_LifecycleListener and answers IsSuspended.
 *
 * Consumers (the GameFlow pause controller) register a listener and never depend on the Platform module's
 * concrete type. Degrades inert (no auto-pause) when no adapter is registered. The adapter owns the raw
 * engine-delegate registration and removes it on teardown, so listeners never touch FCoreDelegates.
 */
class DESIGNPATTERNSSEAMS_API ISeam_AppLifecycle
{
	GENERATED_BODY()

public:
	/** True while the application is currently suspended / backgrounded / unfocused. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	bool IsSuspended() const;

	/** Register a listener to receive suspend/resume callbacks. Safe to call with an already-registered listener (deduped). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void RegisterLifecycleListener(const TScriptInterface<ISeam_LifecycleListener>& Listener);

	/** Unregister a previously-registered listener. Safe to call with an unknown listener (no-op). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void UnregisterLifecycleListener(const TScriptInterface<ISeam_LifecycleListener>& Listener);
};
