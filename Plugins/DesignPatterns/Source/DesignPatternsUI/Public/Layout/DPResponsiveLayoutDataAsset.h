// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Layout/DPLayoutTypes.h"
#include "DPResponsiveLayoutDataAsset.generated.h"

/**
 * Data-driven thresholds + service keys for the responsive layout subsystem.
 *
 * NO magic numbers live in code: the width boundaries that separate the breakpoints, the poll
 * cadence, and the service keys the subsystem resolves are all here. Identity is the inherited
 * DataTag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_ResponsiveLayoutDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Upper viewport-WIDTH bound (in px, at native resolution) for the Compact class. Below this
	 * width the layout is Compact. Defaults are sensible-but-overridable starting points.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout", meta = (ClampMin = "1"))
	int32 CompactMaxWidth = 800;

	/** Upper width bound for the Medium class. Between CompactMaxWidth and this => Medium. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout", meta = (ClampMin = "1"))
	int32 MediumMaxWidth = 1280;

	/** Upper width bound for the Expanded class. Between MediumMaxWidth and this => Expanded; above => Wide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout", meta = (ClampMin = "1"))
	int32 ExpandedMaxWidth = 2200;

	/**
	 * How often (seconds) the subsystem re-polls the safe-zone provider for resolution/inset/DPI
	 * changes. Many platforms do not push these, so a light poll is the robust path. Defaults to a
	 * gentle cadence; set 0 to disable polling (push-only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout", meta = (ClampMin = "0.0"))
	float PollIntervalSeconds = 0.5f;

	/** Service-locator key under which the ISeam_SafeZoneProvider is published. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	FGameplayTag SafeZoneServiceKey;

	/** Service-locator key under which the accessibility provider is published (for the register handshake). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|UI|Layout")
	FGameplayTag AccessibilityServiceKey;

	/** Classify Width (px) into a breakpoint using the configured thresholds. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Layout")
	EDP_UIBreakpoint ClassifyWidth(int32 Width) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
