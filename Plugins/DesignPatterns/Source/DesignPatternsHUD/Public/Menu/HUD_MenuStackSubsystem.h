// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "HUD_MenuStackSubsystem.generated.h"

class UDP_ViewBase;
class UDP_ViewModelBase;
class UDP_UIManagerSubsystem;

/** Broadcast whenever the menu stack depth changes (a screen was pushed or popped). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FHUD_OnMenuStackChanged, int32, NewDepth);

/**
 * Bus payload for a "push menu screen" intent (HUDTags::Bus_MenuPush). A publisher (e.g. a HUD button view
 * or a gameplay system) broadcasts this to request a screen open by tag without referencing this subsystem.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_MenuPushRequest
{
	GENERATED_BODY()

	/** Registry key of the screen to push (DP.UI.Screen.*). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|HUD|Menu")
	FGameplayTag ScreenTag;
};

/**
 * Local-player-scoped navigation controller for menu screens, layered on top of the core UI mediator.
 *
 * The core UDP_UIManagerSubsystem owns the actual widget push/pop onto layer stacks; this subsystem adds
 * the menu-navigation policy on top of it:
 *  - PushMenu / PopMenu / PopToRoot delegate widget realisation to the mediator (by screen tag) while
 *    maintaining a tag history so back-navigation is deterministic.
 *  - While ANY menu screen is open it PUSHES an input mode through the shared ISeam_InputModeArbiter
 *    (resolved from the service locator), and POPS it when the last menu closes — so opening a menu hands
 *    input ownership to the UI without this module reinventing input config. The arbiter is owned by the
 *    Platform module; this subsystem depends only on the seam.
 *  - HandleBack() implements one "back / cancel" step (consumed from the menu-back bus intent or a UI
 *    button) — popping the top screen, or doing nothing if the stack is empty.
 *  - On push it requests gamepad focus onto the new screen's widget so controller players land on a
 *    focusable element; cosmetic-only, guarded for keyboard/mouse.
 *
 * Purely local UI state — never replicated.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_MenuStackSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Fired after the stack depth changes (push/pop). NewDepth is the post-change count. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|HUD|Menu")
	FHUD_OnMenuStackChanged OnMenuStackChanged;

	/**
	 * Push a menu screen by tag. The core mediator realises the widget on the configured menu layer; this
	 * subsystem records the tag, ensures the input-mode lock is held, and focuses the new screen.
	 *
	 * @param ScreenTag  Registry key of the screen to show (DP.UI.Screen.*).
	 * @param ViewModel  Optional ViewModel to assign to the created view.
	 * @return The created view, or null on failure (unknown tag / mediator unavailable).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Menu", meta = (AdvancedDisplay = "ViewModel"))
	UDP_ViewBase* PushMenu(FGameplayTag ScreenTag, UDP_ViewModelBase* ViewModel = nullptr);

	/**
	 * Pop the top menu screen. When the stack empties, the input-mode lock is released. Re-focuses the new
	 * top screen for gamepad players.
	 * @return true if a screen was popped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Menu")
	bool PopMenu();

	/** Pop every menu screen and release the input-mode lock. @return number of screens popped. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Menu")
	int32 PopToRoot();

	/**
	 * Perform one "back / cancel" navigation step — equivalent to PopMenu when a menu is open. Bound to the
	 * menu-back bus intent and the UI back button. @return true if it consumed the back (a screen was open).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Menu")
	bool HandleBack();

	/** Current number of open menu screens. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Menu")
	int32 GetStackDepth() const { return ScreenHistory.Num(); }

	/** True while at least one menu screen is open (and the input-mode lock is held). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Menu")
	bool IsMenuOpen() const { return ScreenHistory.Num() > 0; }

	/** The screen tag currently on top of the menu stack, or an empty tag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Menu")
	FGameplayTag GetTopScreen() const { return ScreenHistory.Num() > 0 ? ScreenHistory.Last() : FGameplayTag(); }

private:
	/** Resolve the core UI mediator (GameInstance-scoped), or null. */
	UDP_UIManagerSubsystem* GetUIManager() const;

	/** Acquire the input-mode lock via the arbiter seam if not already held (called on first push). */
	void AcquireInputModeLock();

	/** Release the input-mode lock via the arbiter seam if held (called when the stack empties). */
	void ReleaseInputModeLock();

	/** Resolve the configured menu UI-layer tag (developer settings, defensive fallback if unset). */
	FGameplayTag ResolveMenuLayerTag() const;

	/** Move gamepad focus onto Widget so controller players land on a focusable element. Cosmetic, guarded. */
	void FocusWidgetForGamepad(UDP_ViewBase* Widget);

	/** Subscribe to the menu-back / menu-push bus intents so UI/input can drive navigation by tag. */
	void RegisterBusListeners();

	/** Ordered history of open screen tags (index 0 = root, last = top). Drives deterministic back-nav. */
	TArray<FGameplayTag> ScreenHistory;

	/** Request id returned by the arbiter's PushInputMode; pop exactly this to release the lock. */
	FGuid InputModeRequestId;

	/** True while we currently hold an input-mode push on the arbiter. */
	bool bHoldingInputMode = false;
};
