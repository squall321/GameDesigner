// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "HUD_ReticleConfigDataAsset.generated.h"

/**
 * Data-driven tuning for the crosshair/reticle (UHUD_ReticleSubsystem): the forward target trace,
 * hit-confirm timing, spread defaults, and the reflected spread-payload field name. No magic numbers in code.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Reticle Config"))
class DESIGNPATTERNSHUD_API UHUD_ReticleConfigDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// --- Forward target trace (for team coloring) ---

	/** When true, the reticle traces forward from the camera each tick to resolve the centre target's team. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Trace")
	bool bTraceForTargetType = true;

	/** Trace channel for the forward target trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Trace")
	TEnumAsByte<ECollisionChannel> TargetTraceChannel = ECC_Visibility;

	/** Length (uu) of the forward target trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Trace", meta = (ClampMin = "1.0"))
	float TargetTraceLength = 10000.f;

	// --- Hit confirm ---

	/** Seconds the hit-confirm marker stays fully visible before fading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|HitConfirm", meta = (ClampMin = "0.0"))
	float HitConfirmHoldSeconds = 0.15f;

	/** Seconds over which the hit-confirm marker fades to zero after the hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|HitConfirm", meta = (ClampMin = "0.01"))
	float HitConfirmFadeSeconds = 0.25f;

	// --- Spread ---

	/** Resting spread (degrees) when no producer has driven a spread value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Spread", meta = (ClampMin = "0.0"))
	float DefaultSpreadDegrees = 1.f;

	/** Field name of the float spread on the spread bus payload (default "SpreadDegrees"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Spread")
	FName SpreadFieldName = FName(TEXT("SpreadDegrees"));

	// --- Hit-feedback payload (for hit confirm) ---

	/** Field name of the INSTIGATOR actor on the hit-feedback payload (default "Instigator"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Payload")
	FName InstigatorFieldName = FName(TEXT("Instigator"));

	/** Field name of the float damage amount on the hit-feedback payload (default "BaseDamage"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Payload")
	FName AmountFieldName = FName(TEXT("BaseDamage"));

	/** Field name of the classification tag on the hit-feedback payload (default "SourceTag"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Payload")
	FName ClassificationFieldName = FName(TEXT("SourceTag"));

	/** Classification tags that mark a hit confirm as a critical (a stronger hit-confirm pulse). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Reticle|Payload")
	FGameplayTagContainer CritTags;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("HUD_ReticleConfig")); }
	//~ End UDP_DataAsset
};
