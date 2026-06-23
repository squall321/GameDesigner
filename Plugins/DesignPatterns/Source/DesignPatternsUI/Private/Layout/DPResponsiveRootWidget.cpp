// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Layout/DPResponsiveRootWidget.h"
#include "Layout/DPResponsiveLayoutSubsystem.h"
#include "Core/DPLog.h"

#include "Components/SafeZone.h"
#include "Components/NamedSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/LocalPlayer.h"

void UDP_ResponsiveRootWidget::SetLayoutVariantForBreakpoint(EDP_UIBreakpoint Breakpoint, TSubclassOf<UWidget> Variant)
{
	BreakpointVariants.Add(Breakpoint, Variant);
	if (IsConstructed())
	{
		ApplyLayout();
	}
}

void UDP_ResponsiveRootWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UDP_ResponsiveLayoutSubsystem* Layout = GetLayoutSubsystem())
	{
		Layout->OnLayoutChanged.AddDynamic(this, &UDP_ResponsiveRootWidget::HandleLayoutChanged);
	}

	ApplyLayout();
}

void UDP_ResponsiveRootWidget::NativeDestruct()
{
	if (UDP_ResponsiveLayoutSubsystem* Layout = GetLayoutSubsystem())
	{
		Layout->OnLayoutChanged.RemoveDynamic(this, &UDP_ResponsiveRootWidget::HandleLayoutChanged);
	}
	Super::NativeDestruct();
}

UDP_ResponsiveLayoutSubsystem* UDP_ResponsiveRootWidget::GetLayoutSubsystem() const
{
	if (const ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		return LP->GetSubsystem<UDP_ResponsiveLayoutSubsystem>();
	}
	return nullptr;
}

void UDP_ResponsiveRootWidget::ApplyLayout()
{
	UDP_ResponsiveLayoutSubsystem* Layout = GetLayoutSubsystem();
	const FMargin Margin = Layout ? Layout->GetSafeZoneMargin() : FMargin(0);
	const EDP_UIBreakpoint Breakpoint = Layout ? Layout->GetBreakpoint() : EDP_UIBreakpoint::Expanded;

	// Drive the safe-zone padding. Prefer the engine USafeZone when bound; else fall back to the
	// content slot padding so safe-area still applies without a SafeZone in the tree.
	if (SafeZoneBox)
	{
		SafeZoneBox->SetSafeAreaScale(FMargin(1.f)); // honour full safe area
		SafeZoneBox->SetPadding(Margin);
	}

	ApplyVariant(Breakpoint);
}

void UDP_ResponsiveRootWidget::ApplyVariant(EDP_UIBreakpoint Breakpoint)
{
	if (!ContentSlot)
	{
		// Nothing to inject into — margin was still applied above; this is a valid no-variant root.
		return;
	}

	// Skip the rebuild when the breakpoint's variant is already shown.
	if (bHasAppliedVariant && Breakpoint == CurrentVariantBreakpoint && CurrentVariant)
	{
		return;
	}

	const TSubclassOf<UWidget>* VariantClassPtr = BreakpointVariants.Find(Breakpoint);
	if (!VariantClassPtr || !*VariantClassPtr)
	{
		// No variant declared for this breakpoint: keep whatever content is present.
		return;
	}

	UWidget* NewVariant = WidgetTree
		? WidgetTree->ConstructWidget<UWidget>(*VariantClassPtr)
		: nullptr;
	if (!NewVariant)
	{
		UE_LOG(LogDP, Warning, TEXT("[ResponsiveRoot] Failed to construct variant for breakpoint %d on %s."),
			static_cast<int32>(Breakpoint), *GetName());
		return;
	}

	// A named slot is a content widget — setting its content replaces the previous variant.
	ContentSlot->SetContent(NewVariant);
	CurrentVariant = NewVariant;
	CurrentVariantBreakpoint = Breakpoint;
	bHasAppliedVariant = true;
}

void UDP_ResponsiveRootWidget::HandleLayoutChanged(EDP_UIBreakpoint /*NewBreakpoint*/)
{
	ApplyLayout();
}
