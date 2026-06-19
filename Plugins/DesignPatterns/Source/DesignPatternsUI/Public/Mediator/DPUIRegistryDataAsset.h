// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Misc/EngineVersionComparison.h"
#include "DPUIRegistryDataAsset.generated.h"

class UDP_ViewBase;

/**
 * One screen definition: maps a screen identity tag to the widget class that
 * implements it and the named layer tag it is pushed onto.
 *
 * WidgetClass is a soft reference so the registry can be authored and shipped
 * without hard-loading every screen up front; the manager loads it on demand.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSUI_API FDP_ScreenDef
{
	GENERATED_BODY()

	/** Stable identity of the screen (e.g. DP.UI.Screen.MainMenu). Used as the registry key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI",
		meta = (Categories = "DP.UI"))
	FGameplayTag ScreenTag;

	/** The view widget class that realises this screen. Soft so it loads on demand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI")
	TSoftClassPtr<UDP_ViewBase> WidgetClass;

	/** The layer this screen belongs to (e.g. DP.UI.Layer.Menu / DP.UI.Layer.Modal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI",
		meta = (Categories = "DP.UI"))
	FGameplayTag LayerTag;

	/** ZOrder used when adding the screen widget to the viewport on its layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI")
	int32 ZOrder = 0;
};

/**
 * Authoring asset: the catalogue of every screen the UI mediator can push.
 *
 * The UDP_UIManagerSubsystem consults this registry to resolve a ScreenTag into
 * a widget class + target layer, decoupling call sites (which only know tags)
 * from concrete widget classes. Implemented as a UPrimaryDataAsset so it can be
 * discovered/loaded through the asset manager.
 */
UCLASS(BlueprintType, meta = (DisplayName = "DP UI Registry"))
class DESIGNPATTERNSUI_API UDP_UIRegistryDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Every screen this registry knows about. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI",
		meta = (TitleProperty = "ScreenTag"))
	TArray<FDP_ScreenDef> Screens;

	/**
	 * Find a screen definition by its tag.
	 * @return Pointer into the Screens array, or null if not registered.
	 */
	const FDP_ScreenDef* FindScreen(const FGameplayTag& ScreenTag) const;

	/** Blueprint-friendly lookup; bFound communicates success without exposing a raw pointer. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI")
	bool GetScreenDef(FGameplayTag ScreenTag, FDP_ScreenDef& OutDef) const;

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	//~ Begin UObject
	// UE 5.4+ validates through FDataValidationContext; 5.3 used a TArray<FText> out-param.
#if UE_VERSION_OLDER_THAN(5, 4, 0)
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#else
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
	//~ End UObject
#endif
};
