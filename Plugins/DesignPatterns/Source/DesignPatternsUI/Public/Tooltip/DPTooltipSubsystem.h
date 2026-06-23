// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Tooltip/DPTooltipTypes.h"
#include "DPTooltipSubsystem.generated.h"

class UWidget;
class UUserWidget;
class UDP_ViewBase;
class UDP_ViewModelBase;

/**
 * Centralised, per-local-player tooltip controller.
 *
 * Hover sources (UDP_TooltipSourceWidget, or any caller) request a tooltip; the subsystem applies a
 * configurable hover DELAY before showing, positions the tooltip either following the cursor or
 * anchored to the source (clamped to the responsive layout's safe-zone margin), and shows a rich
 * MVVM tooltip — a UDP_ViewBase whose content is a UDP_ViewModelBase, so tooltip content reuses the
 * standard binding path rather than ad-hoc text.
 *
 * It pulls the tooltip widget from UDP_WidgetPoolSubsystem (tooltips churn a lot) and returns it on
 * dismiss. It ticks via FTSTicker only WHILE a tooltip follows the cursor, and tears the ticker
 * down deterministically. Debug is a plain getter (the engine LocalPlayer base declares no DP hook).
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_TooltipSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Request a tooltip for HoverSource. After the global hover delay elapses (unless already
	 * elapsed), a TooltipClass widget is acquired from the pool, bound to Content, positioned per
	 * Follow, and shown. Re-requesting for the same source updates the pending/active content.
	 *
	 * @param HoverSource  The widget being hovered (identity + anchor geometry source).
	 * @param TooltipClass The rich tooltip view class to show.
	 * @param Content      The ViewModel that drives the tooltip's content (may be null).
	 * @param Follow       Follow-cursor vs anchored-to-source positioning.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Tooltip", meta = (AdvancedDisplay = "Follow"))
	void RequestTooltip(UWidget* HoverSource, TSubclassOf<UDP_ViewBase> TooltipClass,
		UDP_ViewModelBase* Content, EDP_TooltipFollow Follow = EDP_TooltipFollow::FollowCursor);

	/** Dismiss the tooltip currently shown for (or pending for) HoverSource. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Tooltip")
	void DismissTooltip(UWidget* HoverSource);

	/** Set the hover delay (seconds) before a requested tooltip appears. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Tooltip")
	void SetGlobalHoverDelay(float Seconds);

	/** The current hover delay in seconds. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Tooltip")
	float GetGlobalHoverDelay() const { return HoverDelaySeconds; }

	/** True while a tooltip is currently visible. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Tooltip")
	bool IsTooltipVisible() const { return ActiveTooltip != nullptr; }

	/** One-line status string for on-screen debug. */
	FString GetDebugString() const;

private:
	/** Show the pending tooltip now (delay elapsed): acquire from pool, bind, position, display. */
	void ShowPending();

	/** Hide + recycle the active tooltip widget back to the pool. */
	void HideActive();

	/** Reposition the active tooltip (follow cursor or stay anchored), clamped to the safe zone. */
	void UpdatePosition();

	/** FTSTicker callback while following the cursor / waiting out the delay. */
	bool TickTooltip(float DeltaTime);

	/** Ensure the per-frame ticker is running; started lazily only when needed. */
	void EnsureTicker();

	/** Stop the per-frame ticker. */
	void TeardownTicker();

	/** Configured hover delay (seconds). Sourced from SetGlobalHoverDelay; defensive default. */
	UPROPERTY()
	float HoverDelaySeconds = 0.4f;

	/** Offset (px) of a follow-cursor tooltip from the cursor hotspot. */
	UPROPERTY()
	FVector2D CursorOffset = FVector2D(16.0f, 16.0f);

	/** The source the pending/active tooltip belongs to. */
	UPROPERTY()
	TWeakObjectPtr<UWidget> PendingSource;

	/** The pending tooltip view class (before the delay elapses). */
	UPROPERTY()
	TSubclassOf<UDP_ViewBase> PendingClass = nullptr;

	/** The pending tooltip content ViewModel. */
	UPROPERTY()
	TObjectPtr<UDP_ViewModelBase> PendingContent = nullptr;

	/** The pending follow mode. */
	UPROPERTY()
	EDP_TooltipFollow PendingFollow = EDP_TooltipFollow::FollowCursor;

	/** The currently-shown tooltip widget (owning ref; pool-owned, parked on dismiss). */
	UPROPERTY()
	TObjectPtr<UDP_ViewBase> ActiveTooltip = nullptr;

	/** The follow mode of the active tooltip. */
	UPROPERTY()
	EDP_TooltipFollow ActiveFollow = EDP_TooltipFollow::FollowCursor;

	/** Seconds the pending request has been hovering (counts up to HoverDelaySeconds). */
	float PendingElapsed = 0.0f;

	/** True once the pending tooltip has been promoted to active. */
	bool bPendingShown = false;

	/** Per-frame ticker handle; removed on teardown. */
	FTSTicker::FDelegateHandle TickerHandle;
};
