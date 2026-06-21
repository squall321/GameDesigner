// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_UIHighlight.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_UIHighlight : public UInterface
{
	GENERATED_BODY()
};

/**
 * UI element highlight seam. The HUD / inventory UI / a project's UMG implements it; the tutorial system
 * highlights a named UI target (a HUD slot, an inventory cell) during a step without depending on the HUD
 * module. Targets and styles are tag-keyed and data-driven.
 */
class DESIGNPATTERNSSEAMS_API ISeam_UIHighlight
{
	GENERATED_BODY()

public:
	/** Begin highlighting the UI element identified by TargetTag with the given style. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|UI")
	void HighlightTarget(FGameplayTag TargetTag, FGameplayTag StyleTag);

	/** Stop highlighting TargetTag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|UI")
	void ClearHighlight(FGameplayTag TargetTag);
};
