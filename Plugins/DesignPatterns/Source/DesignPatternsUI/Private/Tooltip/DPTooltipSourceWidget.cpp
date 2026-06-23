// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tooltip/DPTooltipSourceWidget.h"
#include "Tooltip/DPTooltipSubsystem.h"
#include "Engine/LocalPlayer.h"

void UDP_TooltipSourceWidget::SetTooltipContent(UDP_ViewModelBase* Content)
{
	TooltipContent = Content;
}

UDP_TooltipSubsystem* UDP_TooltipSourceWidget::GetTooltipSubsystem() const
{
	if (const ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		return LP->GetSubsystem<UDP_TooltipSubsystem>();
	}
	return nullptr;
}

UDP_ViewModelBase* UDP_TooltipSourceWidget::BuildTooltipContent_Implementation()
{
	// Default: use whatever content was explicitly set. Subclasses/BP override to build a fresh VM.
	return TooltipContent;
}

void UDP_TooltipSourceWidget::RequestShow()
{
	if (!TooltipClass)
	{
		return;
	}

	UDP_TooltipSubsystem* Subsystem = GetTooltipSubsystem();
	if (!Subsystem)
	{
		return;
	}

	UDP_ViewModelBase* Content = BuildTooltipContent();
	Subsystem->RequestTooltip(this, TooltipClass, Content, Follow);
}

void UDP_TooltipSourceWidget::RequestHide()
{
	if (UDP_TooltipSubsystem* Subsystem = GetTooltipSubsystem())
	{
		Subsystem->DismissTooltip(this);
	}
}

void UDP_TooltipSourceWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	RequestShow();
}

void UDP_TooltipSourceWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	RequestHide();
	Super::NativeOnMouseLeave(InMouseEvent);
}

void UDP_TooltipSourceWidget::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	Super::NativeOnAddedToFocusPath(InFocusEvent);
	// Gamepad navigation lands focus here — show the tooltip just like a hover.
	RequestShow();
}

void UDP_TooltipSourceWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	RequestHide();
	Super::NativeOnRemovedFromFocusPath(InFocusEvent);
}

void UDP_TooltipSourceWidget::NativeDestruct()
{
	// Never leave a tooltip pointing at a destroyed source.
	RequestHide();
	Super::NativeDestruct();
}
