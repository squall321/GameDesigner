// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Minimap/HUD_MinimapViewModel.h" // FHUD_MarkerIconRow (tag -> icon table, reused)
#include "HUD_WorldIndicatorConfigDataAsset.generated.h"

/**
 * Data-driven tuning for the world-indicator layer (UHUD_WorldIndicatorSubsystem): the off-screen edge
 * arrows + on-screen threat/objective markers projected from the marker registry.
 *
 * Holds every tunable the indicator subsystem reads — distance fade band, edge-clamp inset, occlusion trace
 * channel, clustering radius, per-frame trace budget, and the tag->icon table — so NOTHING is hard-coded.
 * Identity is the inherited DataTag.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD World Indicator Config"))
class DESIGNPATTERNSHUD_API UHUD_WorldIndicatorConfigDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// --- Distance fade ---

	/** Distance (uu) at/under which an indicator is fully opaque. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Fade", meta = (ClampMin = "0.0"))
	float FadeStartDistance = 1500.f;

	/** Distance (uu) at/over which an indicator is fully faded out (culled). Must exceed FadeStartDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Fade", meta = (ClampMin = "1.0"))
	float FadeEndDistance = 6000.f;

	// --- Edge clamping (off-screen arrows) ---

	/** Inset (pixels) from each viewport edge the off-screen arrows clamp to. Combined with safe-zone insets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Edge", meta = (ClampMin = "0.0"))
	float EdgeInsetPixels = 64.f;

	// --- Occlusion ---

	/** When true, an on-screen indicator does a single line trace to the viewer and dims when occluded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Occlusion")
	bool bOcclusionEnabled = true;

	/** Trace channel used for the occlusion test. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Occlusion")
	TEnumAsByte<ECollisionChannel> OcclusionChannel = ECC_Visibility;

	/** Opacity multiplier applied to an indicator the occlusion trace reports as blocked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Occlusion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OccludedOpacity = 0.35f;

	/** Max occlusion traces performed per refresh (the rest reuse last frame's occlusion result). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Occlusion", meta = (ClampMin = "0"))
	int32 MaxTracesPerRefresh = 16;

	// --- Clustering ---

	/** On-screen indicators within this pixel radius are merged into one clustered indicator (0 disables). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Cluster", meta = (ClampMin = "0.0"))
	float ClusterPixelRadius = 48.f;

	// --- Icons ---

	/** Tag -> icon table (reuses the minimap's FHUD_MarkerIconRow). Resolved per indicator by marker tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator|Icons")
	TArray<FHUD_MarkerIconRow> IconTable;

	/** Effective fade end, guaranteed strictly greater than fade start (defensive against bad authoring). */
	float GetEffectiveFadeEnd() const { return FMath::Max(FadeEndDistance, FadeStartDistance + 1.f); }

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("HUD_WorldIndicatorConfig")); }
	//~ End UDP_DataAsset
};
