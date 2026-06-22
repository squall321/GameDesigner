// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Display/Seam_SafeZoneProvider.h"
#include "Display/UPlat_DisplayTypes.h"
#include "Layout/Margin.h"
#include "UPlat_DisplaySubsystem.generated.h"

/** Broadcast when the resolved display metrics change (resolution / DPI / orientation / safe-area). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnDisplayChanged, const FPlat_DisplayMetrics&, NewMetrics);

/**
 * Computes and caches the current display metrics (resolution, DPI scale, aspect, title-safe + notch
 * insets, orientation) and broadcasts when they change. WRAPS engine sources rather than reinventing:
 *  - viewport resolution from the game viewport client,
 *  - DPI from the user-interface settings curve,
 *  - hardware safe-area from FCoreDelegates::OnSafeFrameChangedEvent (platform-confined),
 * with a settings-tunable title-safe percentage fallback.
 *
 * Implements ISeam_SafeZoneProvider (insets crossed as FVector4 so the seam stays Slate-free) and
 * self-registers under DP.Service.Platform.SafeZone (WeakObserved). UI reads insets/DPI/resolution
 * through the seam; the FMargin conversion lives in UPlat_DisplayLibrary. Skipped on dedicated servers.
 * All FCoreDelegate handles are cleared in Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_DisplaySubsystem : public UDP_GameInstanceSubsystem, public ISeam_SafeZoneProvider
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast whenever the metrics change. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Display")
	FPlat_OnDisplayChanged OnDisplayChanged;

	/** The current resolved metrics snapshot. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	const FPlat_DisplayMetrics& GetDisplayMetrics() const { return Metrics; }

	/** Title-safe insets as an FMargin (px) for direct use by Slate/UMG. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	FMargin GetTitleSafeInsets() const;

	/** Title-safe + display-cutout insets combined (per-edge max) as an FMargin (px). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	FMargin GetCombinedSafeInsets() const;

	/** Force a recompute now (e.g. after the project changes resolution via the settings library). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Display")
	void RefreshMetrics();

	//~ Begin ISeam_SafeZoneProvider
	virtual FVector4 GetSafeInsets_Implementation() const override;
	virtual float GetDPIScale_Implementation() const override;
	virtual FIntPoint GetResolution_Implementation() const override;
	//~ End ISeam_SafeZoneProvider

private:
	/** Recompute Metrics from engine sources; broadcasts OnDisplayChanged if anything changed. */
	void RecomputeMetrics();

	/** Read the platform hardware safe-area (title-safe ratios) into per-edge pixel insets. */
	void ResolveHardwareSafeArea(const FIntPoint& Resolution, FVector4& OutTitleSafePx, FVector4& OutCutoutPx) const;

	/** Register/unregister the safe-zone provider service. */
	void RegisterDisplayService();
	void UnregisterDisplayService();

	/** Engine delegate handlers (forward to RecomputeMetrics). */
	void HandleSafeFrameChanged();
	void HandleViewportResized(class FViewport* Viewport, uint32 Unused);

	/** Cached snapshot. */
	UPROPERTY()
	FPlat_DisplayMetrics Metrics;

	/** FCoreDelegates / viewport handles, all cleared in Deinitialize. */
	FDelegateHandle SafeFrameHandle;
	FDelegateHandle ViewportResizeHandle;

	/** True once the service was registered (so we only unregister our own binding). */
	bool bRegisteredService = false;
};
