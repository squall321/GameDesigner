// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_SafeZoneProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_SafeZoneProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Display-metrics / title-safe seam, owned by the Platform module's display subsystem. UI layout and the
 * camera photo-mode framing read safe-area insets, DPI and resolution through this without depending on the
 * Platform module. Insets are an FVector4 (Left, Top, Right, Bottom in pixels) rather than an FMargin so the
 * Seams module stays free of any Slate dependency; the FMargin conversion lives in a Platform library.
 */
class DESIGNPATTERNSSEAMS_API ISeam_SafeZoneProvider
{
	GENERATED_BODY()

public:
	/** Title-safe insets in pixels: (Left, Top, Right, Bottom). Zero when no safe-zone applies. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Display")
	FVector4 GetSafeInsets() const;

	/** The current UI DPI scale factor (1.0 = unscaled). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Display")
	float GetDPIScale() const;

	/** The current viewport resolution in pixels. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Display")
	FIntPoint GetResolution() const;
};
