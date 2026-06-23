// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DPWidgetPoolable.generated.h"

/**
 * UInterface boilerplate for IDP_WidgetPoolable. Mark any pooled UUserWidget / UDP_ViewBase
 * subclass (in C++ or Blueprint) to receive lifecycle callbacks from UDP_WidgetPoolSubsystem.
 *
 * This deliberately PARALLELS the core IDP_Poolable rather than reusing it: widgets are not
 * AActors, so the core actor-oriented pool (and its transform/collision reset contract) does
 * not apply. The widget pool applies a widget-specific STRUCTURAL reset (RemoveFromParent,
 * clear ViewModel, reset render transform/opacity) and then defers to these hooks for any
 * latent widget state the instance owns.
 */
UINTERFACE(BlueprintType, MinimalAPI, meta = (DisplayName = "DP Widget Poolable"))
class UDP_WidgetPoolable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Lifecycle hooks for pooled widgets.
 *
 * The widget pool reuses UUserWidget instances instead of constructing/destructing them, so an
 * instance's latent state is NOT reset for you beyond the safe structural default applied by
 * UDP_WidgetPoolSubsystem (RemoveFromParent, clear ViewModel, reset render transform + opacity).
 * Any latent state the widget owns — running UWidgetAnimations, bound input/delegate handlers,
 * outstanding UDP_WidgetAnimDriver handles, tooltip registrations, async texture loads — MUST be
 * torn down on return and re-armed on acquire by the widget itself in these hooks.
 *
 * Treat OnReturnedToWidgetPool like NativeDestruct (stop everything you started) and
 * OnAcquiredFromWidgetPool like NativeConstruct (re-arm for a fresh use). All three are
 * BlueprintNativeEvent so designers can author the reset in Blueprint while C++ classes override
 * the *_Implementation. Hooks are invoked by the subsystem on the game thread.
 */
class DESIGNPATTERNSUI_API IDP_WidgetPoolable
{
	GENERATED_BODY()

public:
	/**
	 * Called immediately after the widget is handed out of the pool (after the subsystem's safe
	 * structural reset). Re-arm whatever the widget needs to "go live": restart intro animations,
	 * rebind delegates, reset scroll/selection, etc.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	void OnAcquiredFromWidgetPool();
	virtual void OnAcquiredFromWidgetPool_Implementation() {}

	/**
	 * Called when the widget is returned to the pool, before the subsystem parks it idle. Tear
	 * down ALL latent state here (mirror of OnAcquiredFromWidgetPool) so the next acquirer gets a
	 * clean instance: stop UWidgetAnimations, release UDP_WidgetAnimDriver handles, unregister from
	 * the tooltip subsystem, cancel async loads.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	void OnReturnedToWidgetPool();
	virtual void OnReturnedToWidgetPool_Implementation() {}

	/**
	 * Asked before the pool drains/reclaims an idle widget. Return false to veto reclamation while
	 * the widget is still doing meaningful work it cannot safely abort (e.g. mid fade-out about to
	 * auto-release). Defaults to true. Const-correct: must not mutate the widget.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	bool CanWidgetBeReclaimed() const;
	virtual bool CanWidgetBeReclaimed_Implementation() const { return true; }
};
