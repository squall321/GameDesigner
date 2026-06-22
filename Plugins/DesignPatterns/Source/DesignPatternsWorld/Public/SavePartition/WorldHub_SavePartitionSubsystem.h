// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Save/WorldHub_Snapshot.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "WorldHub_SavePartitionSubsystem.generated.h"

class UWorldHub_GameStateHubSubsystem;
class UWorldHub_SavePartitionPolicyDataAsset;

/** Fired after an async partition save/load completes (game thread). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWorldHub_OnPartitionIO, FGameplayTag, PartitionId, FString, Slot, bool, bSuccess);

/**
 * SAVE PARTITIONING beside the persistent game-state hub (held WEAKLY).
 *
 * Groups persistent slots into named partitions (per-scope / per-chapter) for selective write and
 * load, REUSING the existing BuildSnapshot / ReceiveSnapshot + BuildSaveObject / ApplyFromSave +
 * UWorldHub_SaveGame plumbing — it does NOT change the persistent map's API. Each partition produces
 * its own FWorldHub_Snapshot subset (filtered by the policy) and its own slot file; on selective load
 * a migration map (authored data asset) renames keys before the subset is merged back into the
 * persistent map.
 *
 * GameInstance-scoped, never replicated; pure persistence orchestration.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_SavePartitionSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** The authored partitioning + migration policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|SavePartition")
	TObjectPtr<UWorldHub_SavePartitionPolicyDataAsset> PartitionPolicy;

	/**
	 * Write only the slots belonging to PartitionId to a partition-specific slot file (async). The base
	 * Slot is suffixed per the partition definition. OnPartitionSaved fires on completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|SavePartition")
	void SavePartitionAsync(FGameplayTag PartitionId, const FString& Slot);

	/**
	 * Load a partition slot file (async), apply key migrations, then merge the subset back into the
	 * persistent map (other partitions untouched). OnPartitionLoaded fires on completion.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|SavePartition")
	void LoadPartitionAsync(FGameplayTag PartitionId, const FString& Slot);

	/** Build the snapshot subset belonging to PartitionId from the current persistent map. Out is reset. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|SavePartition")
	void GetPartitionSnapshot(FGameplayTag PartitionId, FWorldHub_Snapshot& Out) const;

	/** Merge a partition snapshot subset back into the persistent map (per-slot ReceiveSnapshot). */
	void MergePartitionSnapshot(FGameplayTag PartitionId, const FWorldHub_Snapshot& In);

	/** Resolve which partition a given slot address belongs to (first-match-wins), or an invalid tag. */
	FGameplayTag ResolvePartitionFor(const FWorldHub_SlotAddress& Address) const;

	/** Compose the partition-specific slot name from a base slot name. */
	FString ComposePartitionSlot(FGameplayTag PartitionId, const FString& BaseSlot) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Fired after SavePartitionAsync completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|SavePartition")
	FWorldHub_OnPartitionIO OnPartitionSaved;

	/** Fired after LoadPartitionAsync completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|SavePartition")
	FWorldHub_OnPartitionIO OnPartitionLoaded;

private:
	/** The persistent hub (held WEAKLY; re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_GameStateHubSubsystem> PersistentHub;

	/** Resolve / cache the persistent game-state hub. */
	UWorldHub_GameStateHubSubsystem* ResolvePersistentHub();

	/** True if the snapshot entry belongs to the named partition (per the policy). */
	bool EntryBelongsToPartition(const FWorldHub_SnapshotEntry& Entry, FGameplayTag PartitionId) const;

	/** Apply the policy's key migrations to a snapshot in place (renaming/dropping keys). */
	void ApplyKeyMigrations(FWorldHub_Snapshot& Snapshot) const;
};
