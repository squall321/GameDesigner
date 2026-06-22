// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"
#include "Persist/Seam_Persistable.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DP_ProfileSaveGame.generated.h"

/**
 * Aggregate record the profile object emits from CaptureState so the generic scatter path can hand the
 * whole shared store to a matching participant in one call. A value-only struct (no UObject refs); carried
 * inside an FInstancedStruct, never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSAVESYSTEM_API FDP_ProfileAggregateRecord
{
	GENERATED_BODY()

	/** Snapshot of the profile's shared records at capture time. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	TArray<FInstancedStruct> SharedRecords;

	/** Parallel kinds for SharedRecords. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	TArray<FGameplayTag> RecordKinds;

	/** Snapshot of unlock flags. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	FGameplayTagContainer Unlocks;
};

/**
 * Concrete PROFILE save object: cross-save shared data that persists independent of any single world save
 * (settings hashes, unlock flags, account-level shared records).
 *
 * Subclass of the core UDP_SaveGame so the core serializer handles it unchanged. It implements
 * ISeam_Persistable as an AGGREGATE store (the same pattern the slot manager uses for world saves): the
 * profile subsystem deposits each profile-kind participant's record via RestoreState during gather, and the
 * profile object exposes the aggregate via CaptureState during scatter.
 *
 * FInstancedStruct is used ONLY inside SaveGame-serialized UPROPERTYs (never replicated) — fully legal.
 */
UCLASS(BlueprintType, Blueprintable)
class DESIGNPATTERNSSAVESYSTEM_API UDP_ProfileSaveGame
	: public UDP_SaveGame
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	/** Cross-save shared records, each tagged by the contributing participant's GetPersistenceKind(). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	TArray<FInstancedStruct> SharedRecords;

	/**
	 * Parallel array to SharedRecords: RecordKinds[i] is the GetPersistenceKind() of SharedRecords[i]. Kept
	 * parallel (not a map) so the SaveGame serializer handles it trivially and order is preserved. The two
	 * arrays are always kept the same length by the mutators below.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	TArray<FGameplayTag> RecordKinds;

	/** Persistent unlock flags (achievements, modes, cosmetics) shared across all world saves. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Save|Profile")
	FGameplayTagContainer Unlocks;

	/** Opaque hash/fingerprint of the player's settings, so a world load can detect settings drift. */
	UPROPERTY(SaveGame, BlueprintReadWrite, Category = "DesignPatterns|Save|Profile")
	FString SettingsHash;

	/** A stable kind tag identifying the profile aggregate (root of the profile-kind family). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save|Profile")
	FGameplayTag ProfileKind;

	// ---- ISeam_Persistable (aggregate store) ----

	/** Aggregate: emit a single record carrying the whole shared store so a load can scatter it. */
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;

	/** Aggregate: absorb an incoming participant record into SharedRecords (deduplicated by kind). */
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;

	/** The profile aggregate's kind tag. */
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	// ---- Helpers ----

	/**
	 * Deposit a participant record under an explicit kind (used by the profile subsystem, which knows each
	 * participant's GetPersistenceKind()). If a record of this kind already exists it is replaced, so the
	 * profile holds at most one record per kind.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	void DepositRecord(FGameplayTag Kind, const FInstancedStruct& Record);

	/** Find the most-recently-deposited shared record whose recorded kind matches (invalid struct if none). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	bool FindSharedRecordByKind(FGameplayTag Kind, FInstancedStruct& Out) const;

	/** Clear all gathered records (called before a fresh gather). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	void ResetGatheredRecords();
};
