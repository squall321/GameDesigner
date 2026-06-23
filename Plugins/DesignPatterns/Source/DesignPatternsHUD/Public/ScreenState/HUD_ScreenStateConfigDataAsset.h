// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "HUD_ScreenStateConfigDataAsset.generated.h"

/**
 * Data-driven tuning for full-screen state effects (UHUD_ScreenStateEffectSubsystem): low-health vignette
 * curve, hit-direction indicator timing, damage-flash timing, and the reflected payload field names. No
 * magic numbers in code.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Screen State Config"))
class DESIGNPATTERNSHUD_API UHUD_ScreenStateConfigDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// --- Low-health vignette ---

	/** Health fraction at/under which the vignette begins to appear (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VignetteStartFraction = 0.35f;

	/** Health fraction at/under which the vignette is at full intensity (0..1). Must be < start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Vignette", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VignetteFullFraction = 0.1f;

	/** Optional curve mapping the normalized [0,1] band position to vignette intensity (linear if null). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Vignette")
	TObjectPtr<class UCurveFloat> VignetteCurve = nullptr;

	// --- Hit-direction indicators ---

	/** Seconds a hit-direction indicator stays fully visible before fading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|HitDir", meta = (ClampMin = "0.0"))
	float HitDirectionHoldSeconds = 0.3f;

	/** Seconds over which a hit-direction indicator fades to zero after the hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|HitDir", meta = (ClampMin = "0.01"))
	float HitDirectionFadeSeconds = 0.7f;

	/** Max concurrent hit-direction indicators (oldest recycled past this). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|HitDir", meta = (ClampMin = "1"))
	int32 MaxHitDirections = 6;

	// --- Damage flash ---

	/** Peak alpha of the full-screen damage flash on a fresh hit (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Flash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DamageFlashPeak = 0.5f;

	/** Seconds over which the damage flash decays to zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Flash", meta = (ClampMin = "0.01"))
	float DamageFlashFadeSeconds = 0.4f;

	// --- Reflected hit-feedback payload field names (no Combat header) ---

	/** Field name of the INSTIGATOR actor on the hit-feedback payload (default "Instigator"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Payload")
	FName InstigatorFieldName = FName(TEXT("Instigator"));

	/** Field name of the FVector impact point on the hit-feedback payload (default "ImpactPoint"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Payload")
	FName ImpactPointFieldName = FName(TEXT("ImpactPoint"));

	/** Field name of the VICTIM actor on the hit-feedback payload (default "HitActor"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Payload")
	FName VictimFieldName = FName(TEXT("HitActor"));

	/** Field name of the float health fraction on the health bus payload (default "Fraction"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Payload")
	FName HealthFractionFieldName = FName(TEXT("Fraction"));

	// --- Optional VFX flash routed to the world VFX seam ---

	/** Optional VFX tag spawned through ISeam_VfxController on a hit (none = no world VFX). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState|Vfx")
	FGameplayTag DamageVfxTag;

	/** Effective vignette full fraction, guaranteed strictly below start (defensive against bad authoring). */
	float GetEffectiveVignetteFull() const { return FMath::Min(VignetteFullFraction, VignetteStartFraction - 0.01f); }

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("HUD_ScreenStateConfig")); }
	//~ End UDP_DataAsset
};
