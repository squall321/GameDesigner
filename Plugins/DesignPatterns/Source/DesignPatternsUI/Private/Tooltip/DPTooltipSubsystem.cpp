// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tooltip/DPTooltipSubsystem.h"
#include "Pool/DPWidgetPoolSubsystem.h"
#include "Layout/DPResponsiveLayoutSubsystem.h"
#include "View/DPViewBase.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Widget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

void UDP_TooltipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDP_TooltipSubsystem::Deinitialize()
{
	HideActive();
	TeardownTicker();
	Super::Deinitialize();
}

void UDP_TooltipSubsystem::SetGlobalHoverDelay(float Seconds)
{
	HoverDelaySeconds = FMath::Max(0.0f, Seconds);
}

void UDP_TooltipSubsystem::RequestTooltip(UWidget* HoverSource, TSubclassOf<UDP_ViewBase> TooltipClass,
	UDP_ViewModelBase* Content, EDP_TooltipFollow Follow)
{
	if (!HoverSource || !TooltipClass)
	{
		return;
	}

	// If a tooltip is already pending/active for a DIFFERENT source, replace it.
	if (PendingSource.Get() != HoverSource)
	{
		HideActive();
		PendingElapsed = 0.0f;
		bPendingShown = false;
	}

	PendingSource = HoverSource;
	PendingClass = TooltipClass;
	PendingContent = Content;
	PendingFollow = Follow;

	// If already shown (e.g. content update while hovering), just rebind content.
	if (bPendingShown && ActiveTooltip)
	{
		ActiveTooltip->SetViewModel(PendingContent);
		return;
	}

	EnsureTicker();
}

void UDP_TooltipSubsystem::DismissTooltip(UWidget* HoverSource)
{
	// Only dismiss if the request belongs to the current source (a stale leave should not nuke a
	// newer hover that already re-requested).
	if (PendingSource.Get() != HoverSource)
	{
		return;
	}

	HideActive();
	PendingSource = nullptr;
	PendingClass = nullptr;
	PendingContent = nullptr;
	PendingElapsed = 0.0f;
	bPendingShown = false;
	TeardownTicker();
}

void UDP_TooltipSubsystem::ShowPending()
{
	if (bPendingShown || !PendingClass || !PendingSource.IsValid())
	{
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;

	UDP_WidgetPoolSubsystem* Pool = LP
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_WidgetPoolSubsystem>(LP->GetGameInstance())
		: nullptr;

	UDP_ViewBase* Tooltip = Pool
		? Pool->AcquireView(PendingClass, PendingContent, PC)
		: nullptr;

	// Defensive fallback: construct directly if the pool is unavailable.
	if (!Tooltip && PC)
	{
		Tooltip = CreateWidget<UDP_ViewBase>(PC, PendingClass);
		if (Tooltip)
		{
			Tooltip->SetViewModel(PendingContent);
		}
	}

	if (!Tooltip)
	{
		UE_LOG(LogDP, Warning, TEXT("[Tooltip] Failed to create tooltip widget."));
		return;
	}

	// Tooltips should not steal hit-testing from the content beneath them.
	Tooltip->SetVisibility(ESlateVisibility::HitTestInvisible);
	Tooltip->AddToViewport(/*ZOrder*/ 10000);

	ActiveTooltip = Tooltip;
	ActiveFollow = PendingFollow;
	bPendingShown = true;

	UpdatePosition();
}

void UDP_TooltipSubsystem::HideActive()
{
	if (!ActiveTooltip)
	{
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	UDP_WidgetPoolSubsystem* Pool = LP
		? FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_WidgetPoolSubsystem>(LP->GetGameInstance())
		: nullptr;

	if (Pool)
	{
		Pool->ReleaseWidget(ActiveTooltip);
	}
	else
	{
		ActiveTooltip->RemoveFromParent();
	}

	ActiveTooltip = nullptr;
}

void UDP_TooltipSubsystem::UpdatePosition()
{
	if (!ActiveTooltip)
	{
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
	if (!PC)
	{
		return;
	}

	FVector2D DesiredPos;

	if (ActiveFollow == EDP_TooltipFollow::FollowCursor)
	{
		float MouseX = 0.f, MouseY = 0.f;
		PC->GetMousePosition(MouseX, MouseY);
		// Mouse position is in viewport pixels; convert to widget-local (DPI) space for SetPositionInViewport.
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(PC);
		const float SafeDPI = (DPI > 0.f) ? DPI : 1.f;
		DesiredPos = FVector2D(MouseX, MouseY) / SafeDPI + CursorOffset;
	}
	else // AnchoredToSource
	{
		UWidget* Source = Cast<UWidget>(PendingSource.Get());
		if (Source)
		{
			const FGeometry& Geo = Source->GetCachedGeometry();
			FVector2D PixelPos, ViewportPos;
			USlateBlueprintLibrary::LocalToViewport(Source, Geo, FVector2D::ZeroVector, PixelPos, ViewportPos);
			DesiredPos = ViewportPos;
		}
	}

	// Clamp into the safe zone so a tooltip near a screen edge stays fully visible.
	if (UDP_ResponsiveLayoutSubsystem* Layout = LP ? LP->GetSubsystem<UDP_ResponsiveLayoutSubsystem>() : nullptr)
	{
		const FMargin Safe = Layout->GetSafeZoneMargin();
		const FIntPoint Res = Layout->GetLayoutState().Resolution;
		const float DPI = FMath::Max(Layout->GetEffectiveDPIScale(), KINDA_SMALL_NUMBER);
		if (Res.X > 0 && Res.Y > 0)
		{
			const FVector2D TooltipSize = ActiveTooltip->GetDesiredSize();
			const float MinX = Safe.Left;
			const float MinY = Safe.Top;
			const float MaxX = (Res.X / DPI) - Safe.Right - TooltipSize.X;
			const float MaxY = (Res.Y / DPI) - Safe.Bottom - TooltipSize.Y;
			DesiredPos.X = FMath::Clamp(DesiredPos.X, MinX, FMath::Max(MinX, MaxX));
			DesiredPos.Y = FMath::Clamp(DesiredPos.Y, MinY, FMath::Max(MinY, MaxY));
		}
	}

	ActiveTooltip->SetPositionInViewport(DesiredPos, /*bRemoveDPIScale*/ false);
}

bool UDP_TooltipSubsystem::TickTooltip(float DeltaTime)
{
	// Source went away (widget destroyed) — clean up.
	if (!PendingSource.IsValid())
	{
		HideActive();
		bPendingShown = false;
		TeardownTicker();
		return false;
	}

	if (!bPendingShown)
	{
		PendingElapsed += DeltaTime;
		if (PendingElapsed >= HoverDelaySeconds)
		{
			ShowPending();
		}
		return true;
	}

	// Active: keep a follow-cursor tooltip glued to the cursor. Anchored tooltips can stop ticking.
	if (ActiveFollow == EDP_TooltipFollow::FollowCursor)
	{
		UpdatePosition();
		return true;
	}

	// Anchored + shown: nothing more to do each frame.
	TeardownTicker();
	return false;
}

void UDP_TooltipSubsystem::EnsureTicker()
{
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UDP_TooltipSubsystem::TickTooltip));
	}
}

void UDP_TooltipSubsystem::TeardownTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

FString UDP_TooltipSubsystem::GetDebugString() const
{
	return FString::Printf(TEXT("Tooltip: %s (delay %.2fs)"),
		ActiveTooltip ? TEXT("visible") : (PendingSource.IsValid() ? TEXT("pending") : TEXT("idle")),
		HoverDelaySeconds);
}
