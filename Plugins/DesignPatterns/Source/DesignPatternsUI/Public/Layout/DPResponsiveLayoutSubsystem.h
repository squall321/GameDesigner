// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Layout/DPLayoutTypes.h"
#include "DPResponsiveLayoutSubsystem.generated.h"

class ISeam_SafeZoneProvider;
class UDP_ResponsiveLayoutDataAsset;

/**
 * Per-local-player responsive layout source of truth.
 *
 * It is the single place that turns raw display metrics (safe-area insets, DPI, resolution) plus
 * the player's accessibility UI-scale into a stable, queryable layout state: a breakpoint class,
 * an FMargin safe-zone, and an effective DPI scale. Widgets read this (and bind OnLayoutChanged)
 * instead of each one reinventing safe-area math.
 *
 * WRAPPING, NOT REINVENTING: display metrics come exclusively from the shared
 * ISeam_SafeZoneProvider (resolved WEAKLY under DP.Service.Platform.SafeZone, never the Platform
 * concrete type). When no provider is present, everything degrades to a defensive default
 * (zero insets, DPI 1.0, breakpoint from the engine viewport size if available).
 *
 * ACCESSIBILITY HANDSHAKE: the accessibility seam is PUSH-only and requires registration. On
 * Initialize this subsystem resolves the accessibility provider from the locator and registers
 * ITSELF as an ISeam_AccessibilityConsumer so OnAccessibilityOptionsChanged is actually delivered;
 * it caches the last options (UIScale defaults 1.0 when no provider).
 *
 * This is a ULocalPlayerSubsystem (per-viewport, so split-screen players classify independently)
 * and sits BESIDE UDP_UILayoutSubsystem without touching it. It exposes a plain GetDebugString()
 * rather than overriding a DP base hook, because the engine LocalPlayer base declares none.
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_ResponsiveLayoutSubsystem : public ULocalPlayerSubsystem, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AccessibilityConsumer
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/** The resolved size class. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Layout")
	EDP_UIBreakpoint GetBreakpoint() const { return State.Breakpoint; }

	/** Title-safe insets as an FMargin (Left, Top, Right, Bottom px). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Layout")
	FMargin GetSafeZoneMargin() const { return State.SafeZoneMargin; }

	/** Effective DPI scale = platform DPI scale * accessibility UI scale. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Layout")
	float GetEffectiveDPIScale() const { return State.EffectiveDPIScale; }

	/** The full current layout snapshot. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Layout")
	const FDP_LayoutState& GetLayoutState() const { return State; }

	/** Assign / swap the thresholds + service-key data asset and recompute immediately. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Layout")
	void SetLayoutDataAsset(UDP_ResponsiveLayoutDataAsset* InDataAsset);

	/** Force an immediate recompute (e.g. after a known viewport-resize event). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Layout")
	void RefreshNow();

	/** Fired when the breakpoint, safe margin, or effective DPI changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|UI|Layout")
	FDP_OnLayoutChanged OnLayoutChanged;

	/** One-line status string for on-screen debug (not a DP base override — engine base has none). */
	FString GetDebugString() const;

private:
	/** Resolve the safe-zone provider weakly from the locator under the configured key. */
	TScriptInterface<ISeam_SafeZoneProvider> ResolveSafeZoneProvider() const;

	/** Register self as an accessibility consumer with the provider (push handshake). Idempotent-ish. */
	void RegisterAsAccessibilityConsumer();

	/** Recompute State from the provider + accessibility scale; broadcast if anything meaningful changed. */
	void Recompute();

	/** FTSTicker poll callback that periodically re-runs Recompute. */
	bool TickPoll(float DeltaTime);

	/** Resolve the active data asset, defaulting to the project's default object if none is set. */
	const UDP_ResponsiveLayoutDataAsset* GetEffectiveDataAsset() const;

	/** The active thresholds/keys asset. */
	UPROPERTY()
	TObjectPtr<UDP_ResponsiveLayoutDataAsset> DataAsset = nullptr;

	/** A defensive default data-asset instance, created when none is assigned, so thresholds always exist. */
	UPROPERTY()
	TObjectPtr<UDP_ResponsiveLayoutDataAsset> DefaultDataAsset = nullptr;

	/** Last computed layout snapshot. */
	UPROPERTY()
	FDP_LayoutState State;

	/** Last accessibility options received (UIScale defaults 1.0 when no provider ever pushed). */
	UPROPERTY()
	FSeam_AccessibilityOptions CachedAccessibility;

	/** Poll ticker; removed in Deinitialize. */
	FTSTicker::FDelegateHandle PollHandle;
};
