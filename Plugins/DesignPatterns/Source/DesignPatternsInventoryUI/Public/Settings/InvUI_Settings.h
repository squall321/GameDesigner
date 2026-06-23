// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// Subclass the CORE developer-settings base (DESIGNPATTERNS_API, a public dependency) rather than
// engine's UDeveloperSettings directly, so this public header never leaks the private
// "DeveloperSettings" module dependency.
#include "Core/DPDeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "InvUI_Settings.generated.h"

/**
 * Project-wide configuration for the deepened InventoryUI features, so NO magic numbers live in
 * code. Appears under Project Settings -> Plugins. Spatial grid sizing, cell pixel size, the
 * per-container-kind default sort/show-empty preferences (persisted by the filter bar), and the
 * drag-trigger distances all read off this settings object.
 *
 * The per-kind sort preference is what the filter bar saves and reloads: it maps a container
 * KindTag (from FInvUI_ContainerInstanceId.KindTag) to the sort-mode tag last chosen for that kind,
 * so a player's "sort bags by type, sort the bank by name" choice survives a session.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns - Inventory UI"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_Settings : public UDP_DeveloperSettings
{
	GENERATED_BODY()

public:
	UInvUI_Settings();

	/** Convenience accessor (never null at runtime once the CDO exists). */
	static const UInvUI_Settings* Get();

	// --- Spatial layout ---

	/** Default column count a spatial (Tetris) grid bin-packs into when a strategy leaves it at 0. */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Spatial", meta = (ClampMin = "1"))
	int32 DefaultSpatialColumns = 10;

	/**
	 * Hard ceiling on rows a spatial auto-pack may grow to, so a corrupt/oversized footprint set
	 * cannot spin the packer forever. 0 = unbounded (the packer still bounds itself by slot count).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Spatial", meta = (ClampMin = "0"))
	int32 MaxSpatialRows = 256;

	/** Pixel size of one layout cell, used by the spatial grid widget to size multi-cell slots. */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Spatial", meta = (ClampMin = "1.0"))
	float DefaultCellPixelSize = 64.f;

	// --- Interaction ---

	/** Minimum pointer travel before a press becomes a drag, for mouse input (slate units). */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Interaction", meta = (ClampMin = "0.0"))
	float MouseDragTriggerDistance = 8.f;

	/** Minimum pointer travel before a press becomes a drag, for touch input (slate units). */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Interaction", meta = (ClampMin = "0.0"))
	float TouchDragTriggerDistance = 16.f;

	// --- Sort/filter preference persistence (written by the filter bar) ---

	/**
	 * Last sort-mode tag chosen per container KindTag. The filter bar saves into this map and
	 * reloads it so a player's per-kind sort choice persists. Defaults empty (the bar then uses
	 * its DefaultSortMode).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Sort")
	TMap<FGameplayTag, FGameplayTag> DefaultSortModeByKind;

	/** Last "show empty slots" choice per container KindTag (filter bar persistence). */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Sort")
	TMap<FGameplayTag, bool> ShowEmptySlotsByKind;

	/** Fallback sort-mode tag used when a kind has no saved preference (may be unset). */
	UPROPERTY(EditAnywhere, Config, Category = "InvUI|Sort")
	FGameplayTag DefaultSortMode;

	// --- Validated accessors (no zero/negative reaches layout math) ---

	/** Spatial column count clamped to >=1. */
	int32 GetEffectiveSpatialColumns() const { return FMath::Max(1, DefaultSpatialColumns); }

	/** Cell pixel size clamped to a sane minimum. */
	float GetEffectiveCellPixelSize() const { return FMath::Max(1.f, DefaultCellPixelSize); }

	/** Saved sort mode for KindTag, or DefaultSortMode when none is stored. */
	FGameplayTag GetSortModeForKind(const FGameplayTag& KindTag) const
	{
		if (const FGameplayTag* Found = DefaultSortModeByKind.Find(KindTag))
		{
			return *Found;
		}
		return DefaultSortMode;
	}

	/** Saved show-empty choice for KindTag, or the supplied fallback when none is stored. */
	bool GetShowEmptyForKind(const FGameplayTag& KindTag, bool bFallback) const
	{
		if (const bool* Found = ShowEmptySlotsByKind.Find(KindTag))
		{
			return *Found;
		}
		return bFallback;
	}
};
