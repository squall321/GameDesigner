// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Seam_NetRelevancyHint.h"
#include "UNet_RelevancyTunerComponent.generated.h"

/** One tier -> concrete net knobs mapping row (data-driven; no hardcoded gameplay numbers). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_RelevancyTierConfig
{
	GENERATED_BODY()

	/** The tier this row configures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy")
	ESeam_NetRelevancyTier Tier = ESeam_NetRelevancyTier::Normal;

	/** Net update frequency (Hz) for actors in this tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "0.1", ClampMax = "120.0"))
	float NetUpdateFrequencyHz = 10.f;

	/** Base net cull distance (uu) for this tier (per-actor bias from the hint is added on top, then clamped). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "100.0"))
	float NetCullDistanceUU = 50000.f;

	/** Whether actors in this tier may go dormant when idle (combined with the hint's WantsDormancyWhenIdle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy")
	bool bAllowDormancy = false;
};

/**
 * Authority-only component that applies bandwidth/relevancy policy to its owning actor by reading the
 * actor's ISeam_NetRelevancyHint (if any) and mapping the resulting tier to concrete net-update-frequency,
 * cull-distance and dormancy values from a data-driven tier table. It centralizes the "make distant /
 * unimportant actors cost less bandwidth" policy so individual actors only advertise a coarse tier.
 *
 * It re-applies on BeginPlay and whenever ApplyNow is called (e.g. after an actor changes importance), all
 * through UNet_RelevancyLibrary so every write is authority-guarded and clamped. The tuner itself never
 * replicates anything — relevancy is a pure server-side bandwidth concern.
 *
 * ENGINE PIECES WRAPPED: SetNetDormancy + the NetUpdateFrequency / NetCullDistanceSquared knobs, surfaced
 * via UNet_RelevancyLibrary so callers don't touch version-sensitive engine fields directly.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_RelevancyTunerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_RelevancyTunerComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/**
	 * Resolve the owner's tier (hint or DefaultTier) and apply its frequency/cull/dormancy to the owner.
	 * AUTHORITY ONLY (no-op on clients). Call after an actor's importance changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	void ApplyNow();

	/** Tier the owner falls back to when it implements no ISeam_NetRelevancyHint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy")
	ESeam_NetRelevancyTier DefaultTier = ESeam_NetRelevancyTier::Normal;

	/** Per-tier knob table. Authored in defaults / a data asset; never hardcoded magic numbers in code. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy")
	TArray<FNet_RelevancyTierConfig> TierConfigs;

	/** Clamp range (uu) the final cull distance is held within after the hint bias is added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "100.0"))
	float MinCullDistanceUU = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "100.0"))
	float MaxCullDistanceUU = 200000.f;

	/** Clamp range (Hz) the final net update frequency is held within. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "0.1"))
	float MinFrequencyHz = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Relevancy", meta = (ClampMin = "0.1"))
	float MaxFrequencyHz = 120.f;

private:
	/** Find the config row for Tier, or a sensible Normal-tier fallback if the table omits it. */
	FNet_RelevancyTierConfig ResolveTierConfig(ESeam_NetRelevancyTier Tier) const;
};
