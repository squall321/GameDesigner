// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Layout/Margin.h"
#include "Display/UPlat_DisplayTypes.h"
#include "UPlat_DisplayLibrary.generated.h"

/**
 * Pure FMargin / FVector2D inset helpers for UI, sitting beside UPlat_StorageLibrary. The conversion
 * from the seam's Slate-free FVector4 insets to a usable FMargin lives HERE (the Platform module
 * depends on SlateCore privately) so the seam itself never needs Slate.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_DisplayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Convert FVector4(Left, Top, Right, Bottom) pixel insets to an FMargin. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	static FMargin InsetsToMargin(const FVector4& Insets);

	/** Per-edge sum of two margins (e.g. add a custom HUD pad on top of the safe-area margin). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	static FMargin InsetMargin(const FMargin& In, const FMargin& Safe);

	/**
	 * Clamp a screen-space position so it stays inside the title-safe rectangle of the given metrics.
	 * @param ScreenPos  Position in pixels.
	 * @param M          The display metrics whose title-safe insets and resolution bound the position.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Display")
	static FVector2D ApplyTitleSafe(FVector2D ScreenPos, const FPlat_DisplayMetrics& M);

	/**
	 * Resolve the current display metrics from the Platform display subsystem via a world context.
	 * Returns a default-constructed snapshot when the subsystem is unavailable (e.g. dedicated server).
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Display", meta = (WorldContext = "WorldContextObject"))
	static FPlat_DisplayMetrics GetCurrentMetrics(const UObject* WorldContextObject);
};
