// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "HUD_StatusBarStyleDataAsset.generated.h"

class UTexture2D;

/** One status-tag -> icon + tint row for the status bar. Matched exactly or as a parent of the status tag. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_StatusIconRow
{
	GENERATED_BODY()

	/** Status (or category) tag this row applies to. Empty rows are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Status")
	FGameplayTag StatusTag;

	/** Soft icon drawn for statuses of this kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Status")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Tint applied to the icon (e.g. green buffs, red debuffs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Status")
	FLinearColor Tint = FLinearColor::White;
};

/**
 * Data-driven icon/tint table for the status/buff bar (UHUD_StatusBarViewModel). Resolves a status's
 * icon + tint by tag (exact, else nearest parent, else a per-category fallback). No magic visuals in code.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Status Bar Style"))
class DESIGNPATTERNSHUD_API UHUD_StatusBarStyleDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Per-status (or per-category) icon/tint rows. The first matching row (exact, else parent) wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	TArray<FHUD_StatusIconRow> Rows;

	/** Default tint applied when no row matches a status tag (icon is left null). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	FLinearColor DefaultTint = FLinearColor::White;

	/** Resolve the best icon+tint for a status tag (out-params); returns false when no row matches. */
	bool ResolveStyle(const FGameplayTag& StatusTag, TSoftObjectPtr<UTexture2D>& OutIcon, FLinearColor& OutTint) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("HUD_StatusBarStyle")); }
	//~ End UDP_DataAsset
};
