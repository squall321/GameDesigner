// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "UPlat_FeatureGateSettings.generated.h"

/**
 * Designer feature flags + editor overrides for the platform feature-gate subsystem. Appears under
 * Project Settings -> Plugins -> Design Patterns Platform Features. Mirrors the UDeveloperSettings
 * idiom (Config=Game, DefaultConfig, GetCategoryName "Plugins", static Get()).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Platform Features"))
class DESIGNPATTERNSPLATFORM_API UPlat_FeatureGateSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPlat_FeatureGateSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. */
	static const UPlat_FeatureGateSettings* Get();

	/**
	 * Hard overrides for tag-keyed features: a feature present here is forced to the mapped bool,
	 * bypassing platform probing (useful to disable a feature on a specific build/config). Missing
	 * feature = use the platform default + the OnQueryFeature designer hook.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Features")
	TMap<FGameplayTag, bool> FeatureOverrides;

	/**
	 * In the editor / PIE there is usually no real online service; when true the feature gate assumes
	 * online/store/presence are available so online-gated UI is testable without a platform login.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Features")
	bool bAssumeOnlineInEditor = true;
};
