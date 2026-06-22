// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Mix/Audio_MixProfileDataAsset.h"
#include "Audio_DuckBusDataAsset.generated.h"

/**
 * DYNAMIC MIXING DEPTH (3) — a data-driven priority-duck sidechain.
 *
 * A duck bus declares: when its DUCKER category is "active" (a VO line plays, a cutscene begins...),
 * a set of DUCKEE categories are scaled down. It is the data behind "dialogue automatically ducks
 * music + SFX" WITHOUT the producer pushing a full mix profile: the VO subsystem (or any caller)
 * resolves a duck bus by tag and pushes it on the sound manager for the duration of the line, then
 * releases it.
 *
 * It reuses the shipped FAudio_DuckRule so the duck composes with every other active mix profile
 * through the EXISTING priority stack (the deepest active duck per category wins). Attack/release are
 * the blend times used when the duck is pushed / popped. DataTag is a child of DP.Audio.Mix.Duck.
 *
 * Purely cosmetic/local; no replication.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_DuckBusDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The category whose activity TRIGGERS this duck (e.g. DP.Audio.Category.Voice). Informational /
	 * for project wiring (the caller decides when to push); used by tooling to group duck buses by the
	 * source that should activate them. Child of DP.Audio.Category.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Duck", meta = (Categories = "DP.Audio.Category"))
	FGameplayTag DuckerCategory;

	/**
	 * The categories scaled down while this duck is held, reusing the shipped duck-rule shape
	 * (target category + linear duck volume; < 0 = project default duck depth). Multiple duckees let a
	 * single VO line duck both music and SFX at different depths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Duck")
	TArray<FAudio_DuckRule> Duckees;

	/**
	 * Stack priority of the pushed duck (higher wins ties). Lets a "dialogue" duck override a weaker
	 * "ambient emphasis" duck. Pure designer ordering value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Duck", meta = (ClampMin = "0", UIMin = "0", UIMax = "100"))
	int32 StackPriority = 50;

	/** Seconds to blend the duck IN when pushed (0 = instant). Designer-tunable; no magic number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Duck", meta = (ClampMin = "0.0", UIMax = "3.0", Units = "s"))
	float AttackSeconds = 0.15f;

	/** Seconds to blend the duck OUT when released (0 = instant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Duck", meta = (ClampMin = "0.0", UIMax = "3.0", Units = "s"))
	float ReleaseSeconds = 0.4f;

	//~ Begin UDP_DataAsset
	/** Own asset-manager bucket ("Audio_DuckBus") so duck buses scan separately from mix profiles. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags duck buses with no duckees or a duckee outside the audio category root. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
