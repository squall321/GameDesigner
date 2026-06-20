// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "Cam_CameraShakeLibrary.generated.h"

class UCameraShakeBase;

/**
 * One row in the shake library: a designer-authored mapping from a stable id/bus-channel tag to a
 * concrete UCameraShakeBase class plus a default intensity scale.
 *
 * The DefaultScale is the entire per-entry tuning knob (NO magic numbers in code): a "heavy hit"
 * row authors a bigger UCameraShakeBase asset and/or a higher DefaultScale; the runtime multiplies
 * it by any per-request scale before handing it to APlayerCameraManager::StartCameraShake.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ShakeEntry
{
	GENERATED_BODY()

	/**
	 * The id this entry answers to. Two complementary uses:
	 *  - direct playback: PlayShakeByTag(ShakeTag, Scale) looks this up exactly;
	 *  - bus-driven playback: when a bus channel (e.g. DP.Bus.Combat.Damaged) is mapped to shakes,
	 *    the listener matches the broadcast channel against these tags (hierarchy-aware).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag ShakeTag;

	/** The concrete shake class to instantiate. May be null (entry is then a documented no-op). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	/**
	 * Baseline intensity multiplied into StartCameraShake's Scale. The caller's per-request scale
	 * is multiplied on top, so the effective scale is DefaultScale * RequestScale. Clamped >= 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultScale = 1.f;

	FCam_ShakeEntry() = default;
};

/**
 * Data-driven library mapping FGameplayTag ids to camera shakes.
 *
 * This is the single place a project authors "what shake plays for what event", with ZERO code
 * coupling between gameplay producers and the camera: combat broadcasts DP.Bus.Combat.Damaged on
 * the message bus, and a row here maps that channel to a shake. Nothing in this module includes a
 * Combat (or any other gameplay) header.
 *
 * Identified by DataTag (from UDP_DataAsset) so the active library is resolvable through the data
 * registry / project settings by stable design-time meaning.
 *
 * COSMETIC / LOCAL ONLY: camera shake is presentation. It is replayed on each machine from already-
 * replicated gameplay (via the bus) and is never itself replicated.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSCAMERA_API UCam_CameraShakeLibrary : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UCam_CameraShakeLibrary();

	/**
	 * The shake entries. Authored as a flat array (rather than a TMap) so designers can reorder /
	 * audit rows easily; the runtime builds a fast lookup index lazily and rebuilds it on edit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	TArray<FCam_ShakeEntry> Entries;

	/**
	 * Find the entry whose ShakeTag exactly equals Tag.
	 * @return pointer to the matching entry, or null if none. Uses a lazily-built index.
	 */
	const FCam_ShakeEntry* FindEntryExact(const FGameplayTag& Tag) const;

	/**
	 * Find the best entry for a broadcast channel using tag-hierarchy matching: prefers an exact
	 * match, otherwise the entry whose ShakeTag is the closest ANCESTOR of Channel (e.g. an entry
	 * keyed "DP.Bus.Combat" answers a broadcast on "DP.Bus.Combat.Damaged.Critical"). The most
	 * specific (deepest) matching ancestor wins so projects can override broadly then narrowly.
	 * @return pointer to the matching entry, or null if none.
	 */
	const FCam_ShakeEntry* FindEntryForChannel(const FGameplayTag& Channel) const;

	//~ Begin UDP_DataAsset
	/** Groups all shake libraries into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject
	/** Invalidates the lookup index so edits take effect immediately in PIE. */
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	/** Warns on duplicate ShakeTag rows and null ShakeClass rows in addition to base validation. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif

private:
	/** Lazily-built ShakeTag -> entry-index map for O(1) exact lookups. Transient; rebuilt on demand. */
	mutable TMap<FGameplayTag, int32> IndexByTag;

	/** True once IndexByTag reflects the current Entries array. */
	mutable bool bIndexBuilt = false;

	/** (Re)build IndexByTag from Entries. */
	void EnsureIndex() const;

	/** Mark the index stale so the next lookup rebuilds it. */
	void InvalidateIndex() const { bIndexBuilt = false; }
};
