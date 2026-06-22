// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Display/UPlat_DisplaySubsystem.h"
#include "Display/UPlat_DisplaySettings.h"
#include "Plat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "UnrealClient.h"
#include "Misc/CoreDelegates.h"
#include "GenericPlatform/GenericApplication.h"

// ---------------------------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------------------------

bool UPlat_DisplaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Display metrics are meaningless on a headless dedicated server.
	return !IsRunningDedicatedServer();
}

void UPlat_DisplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to the engine's hardware safe-frame change (orientation / overscan / notch updates).
	SafeFrameHandle = FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(this, &UPlat_DisplaySubsystem::HandleSafeFrameChanged);

	// Subscribe to viewport resize so DPI/aspect/resolution stay current as the window changes.
	ViewportResizeHandle = FViewport::ViewportResizedEvent.AddUObject(this, &UPlat_DisplaySubsystem::HandleViewportResized);

	RecomputeMetrics();
	RegisterDisplayService();

	UE_LOG(LogDP, Log, TEXT("[Platform] DisplaySubsystem initialized (res=%dx%d dpi=%.2f)."),
		Metrics.ResolutionPx.X, Metrics.ResolutionPx.Y, Metrics.DPIScale);
}

void UPlat_DisplaySubsystem::Deinitialize()
{
	// Remove EVERY registered delegate so we never fire into a dead subsystem.
	if (SafeFrameHandle.IsValid())
	{
		FCoreDelegates::OnSafeFrameChangedEvent.Remove(SafeFrameHandle);
		SafeFrameHandle.Reset();
	}
	if (ViewportResizeHandle.IsValid())
	{
		FViewport::ViewportResizedEvent.Remove(ViewportResizeHandle);
		ViewportResizeHandle.Reset();
	}

	UnregisterDisplayService();

	Super::Deinitialize();
}

FString UPlat_DisplaySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Display %dx%d dpi=%.2f safe=(%.0f,%.0f,%.0f,%.0f)%s"),
		Metrics.ResolutionPx.X, Metrics.ResolutionPx.Y, Metrics.DPIScale,
		Metrics.TitleSafeInsetsPx.X, Metrics.TitleSafeInsetsPx.Y, Metrics.TitleSafeInsetsPx.Z, Metrics.TitleSafeInsetsPx.W,
		Metrics.bIsPortrait ? TEXT(" portrait") : TEXT(""));
}

// ---------------------------------------------------------------------------------------------
//  Service registration
// ---------------------------------------------------------------------------------------------

void UPlat_DisplaySubsystem::RegisterDisplayService()
{
	if (bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		bRegisteredService = Locator->RegisterService(
			Plat_NativeTags::Service_SafeZone, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UPlat_DisplaySubsystem::UnregisterDisplayService()
{
	if (!bRegisteredService)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Locator->ResolveService(Plat_NativeTags::Service_SafeZone) == this)
		{
			Locator->UnregisterService(Plat_NativeTags::Service_SafeZone);
		}
	}
	bRegisteredService = false;
}

// ---------------------------------------------------------------------------------------------
//  Delegate handlers
// ---------------------------------------------------------------------------------------------

void UPlat_DisplaySubsystem::HandleSafeFrameChanged()
{
	RecomputeMetrics();
}

void UPlat_DisplaySubsystem::HandleViewportResized(FViewport* /*Viewport*/, uint32 /*Unused*/)
{
	RecomputeMetrics();
}

void UPlat_DisplaySubsystem::RefreshMetrics()
{
	RecomputeMetrics();
}

// ---------------------------------------------------------------------------------------------
//  Computation
// ---------------------------------------------------------------------------------------------

void UPlat_DisplaySubsystem::RecomputeMetrics()
{
	FPlat_DisplayMetrics New = Metrics;

	// Resolution from the active game viewport, falling back to the previous value if not yet created.
	FIntPoint Resolution = Metrics.ResolutionPx;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UGameViewportClient* VPClient = GI->GetGameViewportClient())
		{
			FVector2D ViewportSize(0, 0);
			VPClient->GetViewportSize(ViewportSize);
			if (ViewportSize.X >= 1.f && ViewportSize.Y >= 1.f)
			{
				Resolution = FIntPoint(FMath::RoundToInt(ViewportSize.X), FMath::RoundToInt(ViewportSize.Y));
			}
		}
	}
	New.ResolutionPx = Resolution;
	New.AspectRatio = (Resolution.Y > 0) ? (float)Resolution.X / (float)Resolution.Y : New.AspectRatio;
	New.bIsPortrait = Resolution.Y > Resolution.X;

	// DPI scale: wrap the engine's resolution-driven UI scale curve.
	New.DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(Resolution);
	if (New.DPIScale <= 0.f)
	{
		New.DPIScale = 1.f; // defensive
	}

	// Hardware / fallback safe-area in pixels.
	ResolveHardwareSafeArea(Resolution, New.TitleSafeInsetsPx, New.DisplayCutoutInsetsPx);

	// Detect a meaningful change before broadcasting (avoid churn on identical recomputes).
	const bool bChanged =
		New.ResolutionPx != Metrics.ResolutionPx ||
		!FMath::IsNearlyEqual(New.DPIScale, Metrics.DPIScale) ||
		New.TitleSafeInsetsPx != Metrics.TitleSafeInsetsPx ||
		New.DisplayCutoutInsetsPx != Metrics.DisplayCutoutInsetsPx ||
		New.bIsPortrait != Metrics.bIsPortrait;

	Metrics = New;

	if (bChanged)
	{
		OnDisplayChanged.Broadcast(Metrics);
		UE_LOG(LogDP, Verbose, TEXT("[Platform] Display metrics changed: %dx%d dpi=%.2f."),
			Metrics.ResolutionPx.X, Metrics.ResolutionPx.Y, Metrics.DPIScale);
	}
}

void UPlat_DisplaySubsystem::ResolveHardwareSafeArea(const FIntPoint& Resolution, FVector4& OutTitleSafePx, FVector4& OutCutoutPx) const
{
	OutTitleSafePx = FVector4(0, 0, 0, 0);
	OutCutoutPx = FVector4(0, 0, 0, 0);

	// Engine-reported safe-zone padding (set by mobile/console platforms; zero otherwise). We wrap
	// FDisplayMetrics::RebuildDisplayMetrics so all platform branching stays out of our code. The
	// padding vectors are per-edge pixel paddings as FVector4(Left, Top, Right, Bottom).
	FDisplayMetrics DisplayMetrics;
	FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);

	// Prefer title-safe; fall back to action-safe when only that is populated.
	FVector4 SafePad = DisplayMetrics.TitleSafePaddingSize;
	if (SafePad.X == 0.f && SafePad.Y == 0.f && SafePad.Z == 0.f && SafePad.W == 0.f)
	{
		SafePad = DisplayMetrics.ActionSafePaddingSize;
	}

	const float HwLeft = (float)SafePad.X;
	const float HwTop = (float)SafePad.Y;
	const float HwRight = (float)SafePad.Z;
	const float HwBottom = (float)SafePad.W;

	const bool bHwSafePresent = (HwLeft + HwTop + HwRight + HwBottom) > 0.f;

	if (bHwSafePresent)
	{
		OutTitleSafePx = FVector4(HwLeft, HwTop, HwRight, HwBottom);
	}
	else if (const UPlat_DisplaySettings* Settings = UPlat_DisplaySettings::Get())
	{
		// Fallback: a uniform title-safe fraction (TV-safe margin). Apply per platform policy.
		const bool bConsoleTarget =
#if PLATFORM_DESKTOP
			false;
#else
			true;
#endif
		if (Settings->bApplyTitleSafeFallbackEverywhere || bConsoleTarget)
		{
			const float Frac = FMath::Clamp(Settings->TitleSafeFallbackFraction, 0.f, 0.2f);
			OutTitleSafePx = FVector4(
				Resolution.X * Frac, Resolution.Y * Frac, Resolution.X * Frac, Resolution.Y * Frac);
		}
	}

	// Display cutout / notch is platform-confined; generic platforms report none.
#if PLATFORM_ANDROID || PLATFORM_IOS
	// The engine exposes the cutout through the safe-zone ratio on mobile; reuse the title-safe values
	// as a conservative cutout estimate when no dedicated cutout API is wired in this build.
	OutCutoutPx = OutTitleSafePx;
#endif
}

// ---------------------------------------------------------------------------------------------
//  FMargin helpers
// ---------------------------------------------------------------------------------------------

FMargin UPlat_DisplaySubsystem::GetTitleSafeInsets() const
{
	return FMargin(
		Metrics.TitleSafeInsetsPx.X, Metrics.TitleSafeInsetsPx.Y,
		Metrics.TitleSafeInsetsPx.Z, Metrics.TitleSafeInsetsPx.W);
}

FMargin UPlat_DisplaySubsystem::GetCombinedSafeInsets() const
{
	return FMargin(
		FMath::Max(Metrics.TitleSafeInsetsPx.X, Metrics.DisplayCutoutInsetsPx.X),
		FMath::Max(Metrics.TitleSafeInsetsPx.Y, Metrics.DisplayCutoutInsetsPx.Y),
		FMath::Max(Metrics.TitleSafeInsetsPx.Z, Metrics.DisplayCutoutInsetsPx.Z),
		FMath::Max(Metrics.TitleSafeInsetsPx.W, Metrics.DisplayCutoutInsetsPx.W));
}

// ---------------------------------------------------------------------------------------------
//  ISeam_SafeZoneProvider
// ---------------------------------------------------------------------------------------------

FVector4 UPlat_DisplaySubsystem::GetSafeInsets_Implementation() const
{
	// Combined per-edge max so consumers get the union of title-safe and notch.
	return FVector4(
		FMath::Max(Metrics.TitleSafeInsetsPx.X, Metrics.DisplayCutoutInsetsPx.X),
		FMath::Max(Metrics.TitleSafeInsetsPx.Y, Metrics.DisplayCutoutInsetsPx.Y),
		FMath::Max(Metrics.TitleSafeInsetsPx.Z, Metrics.DisplayCutoutInsetsPx.Z),
		FMath::Max(Metrics.TitleSafeInsetsPx.W, Metrics.DisplayCutoutInsetsPx.W));
}

float UPlat_DisplaySubsystem::GetDPIScale_Implementation() const
{
	return Metrics.DPIScale;
}

FIntPoint UPlat_DisplaySubsystem::GetResolution_Implementation() const
{
	return Metrics.ResolutionPx;
}
