// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_PlatformAchievements.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_PlatformAchievements : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional bridge to a platform trophy/achievement service (Steam, console). The project supplies the
 * concrete implementation; the achievement subsystem calls it when an achievement unlocks. Held weakly
 * and a no-op when unset, so the framework never depends on a specific platform SDK.
 */
class DESIGNPATTERNSSEAMS_API ISeam_PlatformAchievements
{
	GENERATED_BODY()

public:
	/** Unlock the platform achievement mapped to AchievementTag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void UnlockPlatformAchievement(FGameplayTag AchievementTag);

	/** Report incremental progress [0,1] for a progressive platform achievement. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void SetPlatformAchievementProgress(FGameplayTag AchievementTag, float Normalized);
};
