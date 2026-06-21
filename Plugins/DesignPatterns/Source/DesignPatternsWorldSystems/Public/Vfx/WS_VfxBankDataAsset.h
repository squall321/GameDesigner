// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "WS_VfxBankDataAsset.generated.h"

class UFXSystemAsset;

/**
 * One tag -> particle-system mapping inside a VFX bank.
 *
 * The system is referenced as a soft UFXSystemAsset (the engine base for both Cascade UParticleSystem
 * and Niagara UNiagaraSystem), so the bank — and this module — never hard-depends on the Niagara module.
 * The engine spawn helpers (UGameplayStatics::SpawnEmitterAtLocation/Attached) accept this base type and
 * transparently route to the correct backend.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_VfxEntry
{
	GENERATED_BODY()

	/** Identity key for this effect (child of WS.Vfx). Requested via ISeam_VfxController by tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx", meta = (Categories = "WS.Vfx"))
	FGameplayTag VfxTag;

	/**
	 * The particle system to spawn. Soft so the bank costs only its index until an effect is first used.
	 * Base UFXSystemAsset type keeps this Niagara-module-free; a Cascade or Niagara asset both fit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx")
	TSoftObjectPtr<UFXSystemAsset> System;

	/**
	 * When true the manager recycles this effect's carrier actor through the core object pool instead of
	 * spawning/destroying per use. Recommended for high-frequency one-shots (impacts, footsteps). Looping
	 * attached effects are pooled too but live until StopVfx.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx|Pooling")
	bool bPooled = true;

	/**
	 * Optional per-entry pre-warm count for the pool. A negative value means "use the project default"
	 * (UWS_DeveloperSettings::DefaultVfxPoolWarmup); 0 disables pre-warm for this entry. Ignored when
	 * bPooled is false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx|Pooling", meta = (EditCondition = "bPooled", UIMin = "-1", UIMax = "64"))
	int32 PoolWarmup = -1;

	/**
	 * Hint that this effect loops (does not auto-finish). The manager treats looping systems as tracked
	 * handles that must be explicitly stopped, and never auto-reclaims them on the one-shot timer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx")
	bool bLooping = false;

	/** Uniform cosmetic scale applied to the spawned system. Clamped positive at spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx", meta = (UIMin = "0.01", UIMax = "10.0"))
	float Scale = 1.f;
};

/**
 * Data-driven bank mapping VFX tags to particle systems with pooling hints.
 *
 * The VFX manager registers one or more banks at startup, builds a flat tag -> entry index across all
 * registered banks, and resolves SpawnVfxAtLocation / SpawnVfxAttached requests against it. Identity is
 * the base DataTag (the bank's own id); the per-effect keys are the VfxTags inside Entries.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_VfxBankDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The effects in this bank, keyed by VfxTag. Duplicate tags across banks: first registered wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Vfx", meta = (TitleProperty = "VfxTag"))
	TArray<FWS_VfxEntry> Entries;

	/** Find the entry for VfxTag (exact match), or null. */
	const FWS_VfxEntry* FindEntry(const FGameplayTag& VfxTag) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags entries with an invalid VfxTag or a null system reference. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
