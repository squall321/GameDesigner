// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "Anim/DPWidgetTweenTypes.h"
#include "DPWidgetAnimDriver.generated.h"

class UWidget;
class UUserWidget;
class UCurveFloat;

/**
 * Frame-delta-accurate code tween for any UWidget — fade / slide / scale / rotate / tint.
 *
 * WHY THIS EXISTS: authored UMG sequences (UWidgetAnimation) are great for hand-crafted screens,
 * but pooled/runtime widgets (list rows, world markers, toasts, tooltips) need cheap, uniform,
 * parameterised motion with shared easing. This driver provides that without reinventing the
 * curve system: it samples a designer-authored UCurveFloat by NORMALIZED progress (elapsed /
 * duration), so motion is frame-rate-independent and the "feel" stays in data.
 *
 * TICKING: the driver ticks via FTSTicker, NOT a fixed-interval world timer and NOT
 * FTickableGameObject. FTSTicker is world-agnostic and editor-safe, gives a real frame delta, and
 * is deterministically unregistered on Stop / target loss / BeginDestroy — so a driver never
 * outlives its widget or leaks a ticker.
 *
 * OWNERSHIP: create one with CreateFor(Target); it self-roots (AddToRoot) while playing so it is
 * not GC'd mid-tween, and un-roots when it stops. The target widget is held weakly — if the widget
 * is collected (e.g. removed from the viewport and GC'd), the next tick detects the dead target
 * and stops cleanly.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_WidgetAnimDriver : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Create a driver bound to Target. The driver is outered to Target (so it shares a sensible
	 * lifetime) and additionally self-roots while playing so it is never GC'd mid-tween. Returns
	 * null if Target is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim", meta = (DefaultToSelf = "Target"))
	static UDP_WidgetAnimDriver* CreateFor(UWidget* Target);

	/** Tween render opacity from -> to over Duration with an optional easing curve (null = linear). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim")
	void Fade(float From, float To, float Duration, UCurveFloat* Ease = nullptr);

	/** Tween render-transform translation (px) from -> to over Duration with optional easing. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim")
	void Slide(FVector2D From, FVector2D To, float Duration, UCurveFloat* Ease = nullptr);

	/** Tween render-transform scale from -> to over Duration with optional easing. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim")
	void Scale(FVector2D From, FVector2D To, float Duration, UCurveFloat* Ease = nullptr);

	/** Play an ordered, staggered sequence of steps. Replaces any in-flight play. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim")
	void PlaySequence(const TArray<FDP_WidgetTweenStep>& Steps);

	/**
	 * Stop the current play. When bSnapToEnd is true the target is left at the final value of the
	 * last step; otherwise it is left wherever it currently is. Fires OnFinished(bCompletedFully=false).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Anim")
	void Stop(bool bSnapToEnd);

	/** True while a sequence is actively playing. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Anim")
	bool IsPlaying() const { return bPlaying; }

	/**
	 * When true (default) the driver auto-releases the bound widget back to the widget pool after a
	 * sequence that ends fully — convenient for fade-out-then-recycle toasts. Set false for screens
	 * that own their widget's lifetime themselves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Anim")
	bool bReleaseToPoolOnFinish = false;

	/** Fired once when a play finishes (bCompletedFully=true) or is stopped early (false). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|UI|Anim")
	FDP_OnWidgetTweenFinished OnFinished;

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	/** Begin playing the current Steps from the start; (re)registers the ticker. */
	void StartPlay();

	/** FTSTicker callback — advances the sequence by the real frame delta. */
	bool TickDriver(float DeltaTime);

	/** Apply a single step's interpolated value at normalized progress Alpha (0..1) to the target. */
	void ApplyStep(const FDP_WidgetTweenStep& Step, float Alpha) const;

	/** Sample Ease at Alpha (0..1); linear when Ease is null. */
	static float SampleEase(const UCurveFloat* Ease, float Alpha);

	/** Deterministically remove the ticker and un-root. Safe to call repeatedly. */
	void TeardownTicker();

	/** Finish the play: teardown, optional pool release, broadcast OnFinished, un-root. */
	void FinishPlay(bool bCompletedFully);

	/** The widget being animated. Weak so a collected widget stops the driver instead of resurrecting it. */
	UPROPERTY()
	TWeakObjectPtr<UWidget> WidgetTarget;

	/** The sequence currently playing. */
	UPROPERTY()
	TArray<FDP_WidgetTweenStep> Steps;

	/** Index of the step currently being applied. */
	int32 CurrentStep = 0;

	/** Seconds elapsed within the current step (including its start delay). */
	float StepElapsed = 0.0f;

	/** True between StartPlay and FinishPlay/Stop. */
	bool bPlaying = false;

	/** True while this driver is AddToRoot'd, so we un-root exactly once. */
	bool bRooted = false;

	/** FTSTicker registration handle; reset on teardown so we never double-remove. */
	FTSTicker::FDelegateHandle TickerHandle;
};
