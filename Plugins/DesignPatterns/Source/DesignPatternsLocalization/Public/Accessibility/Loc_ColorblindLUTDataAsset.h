// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Loc_ColorblindLUTDataAsset.generated.h"

/**
 * One per-mode daltonization LUT: a coarse 3D color cube (size N) sampled by ApplyColorblindLUT. The cube
 * stores the corrected color for each quantized (R,G,B) cell so the remap is a designer-authored lookup
 * rather than a fixed formula — letting a project tune hue separation per colorblind mode beyond the
 * canonical inline ApplyColorblindToColor.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLOCALIZATION_API FLoc_ColorblindCube
{
	GENERATED_BODY()

	/**
	 * Per-axis resolution of the cube (cells per channel). The Cells array must be exactly Resolution^3
	 * entries in (R-major, then G, then B) order. Clamped >=2 by Sample's defensive guard; a value of 0/1
	 * means "no LUT" (identity).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Accessibility", meta = (ClampMin = "0", ClampMax = "33"))
	int32 Resolution = 0;

	/** Flattened cube: Resolution^3 corrected colors. Empty => identity (documented inert default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Localization|Accessibility")
	TArray<FLinearColor> Cells;
};

/**
 * Holds an optional daltonization cube per ESeam_ColorblindMode for ULoc_ContrastLibrary::ApplyColorblindLUT.
 * Identity (returns the input unchanged) when a mode has no cube or an inconsistent one — so the library is
 * always safe to call. Identity is the documented inert default; no magic numbers live in code.
 *
 * Identity is the inherited DataTag.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Colorblind LUT"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_ColorblindLUTDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Per-mode correction cubes. A mode absent here samples to identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Localization|Accessibility")
	TMap<ESeam_ColorblindMode, FLoc_ColorblindCube> Cubes;

	/**
	 * Sample the corrected color for In under Mode. Returns In unchanged when the mode has no cube or the
	 * cube is inconsistent (Resolution<2 or Cells.Num() != Resolution^3) — the documented identity fallback.
	 * Trilinear-light: nearest-cell sampling (sufficient for UI palette correction; no per-pixel cost).
	 */
	UFUNCTION(BlueprintPure, Category = "Localization|Accessibility")
	FLinearColor Sample(ESeam_ColorblindMode Mode, const FLinearColor& In) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject
	/** Flags a cube whose Cells count does not match Resolution^3. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
