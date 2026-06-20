// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Seam_AnalyticsSink.generated.h"

/**
 * One analytics event attribute. The value is an FSeam_NetValue (the closed bool/int/float/vector/tag/
 * name union), so an attribute structurally CANNOT carry FText, a raw object, or a free-form id — this is
 * the PII-safety guarantee by construction.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_AnalyticsAttr
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Analytics")
	FName Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Analytics")
	FSeam_NetValue Value;

	FSeam_AnalyticsAttr() = default;
	FSeam_AnalyticsAttr(FName InKey, const FSeam_NetValue& InValue) : Key(InKey), Value(InValue) {}
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_AnalyticsSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * The single analytics seam for the whole framework. Modules (Analytics, LevelDirector, ModContent,
 * Localization) NEVER instantiate an analytics provider; they record aggregate, PII-safe events through
 * this seam, which the host project backs with one adapter that forwards to the engine's
 * IAnalyticsProvider (FAnalytics). Resolved from the service locator by a project-configured tag.
 *
 * Game-thread only. All recording is opt-in and default-OFF (gated by consent in the Analytics module).
 */
class DESIGNPATTERNSSEAMS_API ISeam_AnalyticsSink
{
	GENERATED_BODY()

public:
	/** Record an aggregate event (game thread). Attributes are PII-safe by the FSeam_NetValue constraint. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Analytics")
	void RecordAggregateEvent(FGameplayTag EventTag, const TArray<FSeam_AnalyticsAttr>& Attrs);

	/** True if a real backend is connected and consent has been granted. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Analytics")
	bool IsSinkReady() const;
};
