// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "View/DPViewBase.h"
#include "Layout/DPLayoutTypes.h"
#include "DPResponsiveRootWidget.generated.h"

class USafeZone;
class UNamedSlot;
class UWidget;
class UDP_ResponsiveLayoutSubsystem;

/**
 * A view that applies UDP_ResponsiveLayoutSubsystem output to itself.
 *
 * It pads its content by the live safe-zone margin and swaps a per-breakpoint child layout variant
 * so a single screen can present differently on a phone, a tablet and a TV without separate widget
 * trees. It rebinds on OnLayoutChanged so a resize / split-screen change re-applies automatically.
 *
 * It WRAPS the engine's USafeZone (bound by name from the authored widget tree) rather than
 * reinventing safe-area padding, and reuses UDP_ViewBase's ViewModel binding so responsive screens
 * stay MVVM. The breakpoint variants are designer-authored widget classes (data, not code).
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "DP Responsive Root"))
class DESIGNPATTERNSUI_API UDP_ResponsiveRootWidget : public UDP_ViewBase
{
	GENERATED_BODY()

public:
	/** Register a widget class to instantiate as the content variant for Breakpoint. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Layout")
	void SetLayoutVariantForBreakpoint(EDP_UIBreakpoint Breakpoint, TSubclassOf<UWidget> Variant);

	/** Re-read the layout subsystem and apply margin + the correct variant. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Layout")
	void ApplyLayout();

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/**
	 * Optional bound USafeZone in the authored widget tree. When present, its overrides are driven
	 * from the live safe-zone margin; when absent, ApplyLayout falls back to setting the content
	 * slot's padding so safe-area still works.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "DesignPatterns|UI|Layout")
	TObjectPtr<USafeZone> SafeZoneBox = nullptr;

	/** Named slot the breakpoint variant is injected into. Required for variant swapping. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "DesignPatterns|UI|Layout")
	TObjectPtr<UNamedSlot> ContentSlot = nullptr;

	/**
	 * Designer-authored breakpoint -> content widget class map. Editable on the widget so a screen
	 * declares its variants without code. Empty entries mean "keep the current content".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Layout")
	TMap<EDP_UIBreakpoint, TSubclassOf<UWidget>> BreakpointVariants;

private:
	/** Bound handler for the subsystem's OnLayoutChanged. */
	UFUNCTION()
	void HandleLayoutChanged(EDP_UIBreakpoint NewBreakpoint);

	/** Resolve the per-local-player responsive layout subsystem, or null. */
	UDP_ResponsiveLayoutSubsystem* GetLayoutSubsystem() const;

	/** Instantiate (or reuse) the variant widget for Breakpoint into ContentSlot. */
	void ApplyVariant(EDP_UIBreakpoint Breakpoint);

	/** The currently-shown variant instance (owning ref). */
	UPROPERTY()
	TObjectPtr<UWidget> CurrentVariant = nullptr;

	/** The breakpoint CurrentVariant was built for, to avoid rebuilding on no-op changes. */
	EDP_UIBreakpoint CurrentVariantBreakpoint = EDP_UIBreakpoint::Expanded;

	/** Set once a variant has actually been applied (so the first apply always runs). */
	bool bHasAppliedVariant = false;
};
