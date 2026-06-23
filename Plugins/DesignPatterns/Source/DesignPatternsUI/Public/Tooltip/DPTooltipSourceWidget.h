// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "View/DPViewBase.h"
#include "Tooltip/DPTooltipTypes.h"
#include "DPTooltipSourceWidget.generated.h"

class UDP_ViewModelBase;

/**
 * A view that acts as a hover SOURCE for the tooltip subsystem.
 *
 * On mouse enter (and on gamepad focus) it asks UDP_TooltipSubsystem to show its authored tooltip
 * class with a built content ViewModel; on mouse leave (and focus lost) it dismisses. The hover
 * delay, positioning and pooling are all handled centrally by the subsystem — this widget only
 * declares "what tooltip" and "with what content".
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "DP Tooltip Source"))
class DESIGNPATTERNSUI_API UDP_TooltipSourceWidget : public UDP_ViewBase
{
	GENERATED_BODY()

public:
	/** Set the ViewModel that will drive this source's tooltip content. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Tooltip")
	void SetTooltipContent(UDP_ViewModelBase* Content);

protected:
	//~ Begin UUserWidget
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent) override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/**
	 * The tooltip view class shown for this source. Authored per widget. If unset, no tooltip is
	 * requested (the source is inert).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tooltip")
	TSubclassOf<UDP_ViewBase> TooltipClass = nullptr;

	/** Follow-cursor vs anchored positioning for this source's tooltip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|UI|Tooltip")
	EDP_TooltipFollow Follow = EDP_TooltipFollow::FollowCursor;

	/**
	 * Designer hook to (re)build the tooltip content ViewModel just before it is shown. Override in
	 * Blueprint to populate a content VM from this widget's bound ViewModel. The returned VM (or the
	 * one set via SetTooltipContent) is what the tooltip binds to.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|UI|Tooltip")
	UDP_ViewModelBase* BuildTooltipContent();
	virtual UDP_ViewModelBase* BuildTooltipContent_Implementation();

private:
	/** Resolve the tooltip subsystem for this widget's owning local player, or null. */
	class UDP_TooltipSubsystem* GetTooltipSubsystem() const;

	/** Request the tooltip from the subsystem (no-op if no TooltipClass). */
	void RequestShow();

	/** Dismiss this source's tooltip. */
	void RequestHide();

	/** The explicit content VM (when SetTooltipContent was used). BuildTooltipContent can override. */
	UPROPERTY()
	TObjectPtr<UDP_ViewModelBase> TooltipContent = nullptr;
};
