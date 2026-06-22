// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Data/DPDataAsset.h"
#include "Audio_SurfaceBankDataAsset.generated.h"

/**
 * FOOTSTEP/SURFACE (5) per-surface footstep mapping.
 *
 * Maps a gait (walk/run/land/...) to a SOUND TAG resolved in the normal sound banks, so a footstep
 * is just another tag-keyed one-shot played through the existing IAudio_AudioController. Keeping the
 * value as a sound tag (not a soft USoundBase) means footsteps reuse the sound manager's concurrency,
 * category volume and ducking with zero new playback code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_SurfaceFootstep
{
	GENERATED_BODY()

	/**
	 * Gait -> footstep sound tag. Keys are project gait tags (e.g. DP.Audio.Surface.Gait.Walk); the
	 * value is a DP.Audio.Sound child resolved in a sound bank. The default-gait fallback is the entry
	 * stored under an invalid/empty gait key, or DefaultSound below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ForceInlineRow, Categories = "DP.Audio.Sound"))
	TMap<FGameplayTag, FGameplayTag> ByGait;

	/** Sound tag used when the requested gait is absent from ByGait (child of DP.Audio.Sound). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (Categories = "DP.Audio.Sound"))
	FGameplayTag DefaultSound;
};

/**
 * FOOTSTEP/SURFACE (5) bank: maps physical surface types to per-surface footstep mappings.
 *
 * The physical material's EPhysicalSurface (read from a foot trace / contact) selects the surface
 * entry; the gait then selects the sound tag. Subclass of UDP_DataAsset (DataTag is a child of
 * DP.Audio.Surface). Purely local/cosmetic content.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_SurfaceBankDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Physical surface -> footstep mapping. SurfaceType_Default is the catch-all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	TMap<TEnumAsByte<EPhysicalSurface>, FAudio_SurfaceFootstep> BySurface;

	/** Footstep mapping used when a surface has no explicit entry (overrides per-surface DefaultSound). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	FAudio_SurfaceFootstep FallbackSurface;

	/**
	 * Resolve the footstep SOUND TAG for a surface + gait. Falls back: surface+gait -> surface default
	 * -> bank fallback gait -> bank fallback default. Returns an invalid tag if nothing maps.
	 */
	UFUNCTION(BlueprintCallable, Category = "Surface")
	FGameplayTag ResolveSoundTag(EPhysicalSurface Surface, FGameplayTag Gait) const;

	//~ Begin UDP_DataAsset
	/** Own asset-manager bucket ("Audio_SurfaceBank"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
