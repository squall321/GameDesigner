// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UPlat_DisplaySettings.generated.h"

/**
 * Project configuration for display/safe-zone resolution. Appears under
 * Project Settings -> Plugins -> Design Patterns Platform Display. Mirrors the UDeveloperSettings idiom
 * (Config=Game, DefaultConfig, GetCategoryName "Plugins", static Get()). Holds the title-safe fallback
 * percentage used when the platform exposes no hardware safe-area, so there are no magic numbers in code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Platform Display"))
class DESIGNPATTERNSPLATFORM_API UPlat_DisplaySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPlat_DisplaySettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. Never null in a configured project; null-checked by callers. */
	static const UPlat_DisplaySettings* Get();

	/**
	 * Title-safe inset as a fraction of each axis, applied uniformly when the platform reports no
	 * hardware title-safe zone (e.g. 0.05 = pull HUD in 5% from every edge — the classic TV-safe margin).
	 * Used only as a fallback; a platform-reported safe zone always wins.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Display", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float TitleSafeFallbackFraction = 0.05f;

	/**
	 * When true the title-safe fallback is applied on ALL platforms (not just consoles), useful for a
	 * uniform HUD margin. When false the fallback applies only when the platform is a console/TV target.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Display")
	bool bApplyTitleSafeFallbackEverywhere = false;
};
