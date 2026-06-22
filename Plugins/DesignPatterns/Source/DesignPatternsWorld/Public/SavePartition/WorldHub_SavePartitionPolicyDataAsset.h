// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "WorldHub_SavePartitionPolicyDataAsset.generated.h"

/**
 * One named save partition: the rule that decides which persistent slots belong to it.
 *
 * A slot belongs to a partition when its key matches KeyRoot (tag-parent match) AND, when
 * bRestrictToScopeType is set, its scope is of ScopeType. The first matching partition (in declared
 * order) wins, so place more-specific partitions first. The optional SlotSuffix lets a partition write
 * to a distinct slot file derived from the base slot name.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_SavePartitionDef
{
	GENERATED_BODY()

	/** Stable identity of this partition (e.g. DP.WorldHub.Partition.Chapter1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	FGameplayTag PartitionId;

	/** A slot's key must match this tag (or a child of it) to belong to the partition. Invalid = match all keys. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	FGameplayTag KeyRoot;

	/** When true, also require the slot's scope to be of ScopeType. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	bool bRestrictToScopeType = false;

	/** The required scope type when bRestrictToScopeType is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "bRestrictToScopeType"), Category = "DesignPatterns|WorldHub|SavePartition")
	EWorldHub_ScopeType ScopeType = EWorldHub_ScopeType::Global;

	/** Suffix appended to the base slot name for this partition's file (e.g. "_chapter1"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	FString SlotSuffix;

	FWorldHub_SavePartitionDef() = default;
};

/**
 * A key-migration mapping applied to a loaded partition snapshot BEFORE it is merged back, so an old
 * save's keys can be renamed to the current schema without a destructive resave.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_KeyMigration
{
	GENERATED_BODY()

	/** The key as it appears in an older save. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	FGameplayTag FromKey;

	/** The key it should be renamed to on load (invalid = drop the slot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	FGameplayTag ToKey;

	FWorldHub_KeyMigration() = default;
};

/**
 * The authored partitioning + migration policy for selective world-hub save/load. Subclass of
 * UDP_DataAsset so it is tag-addressable. Purely declarative — no magic numbers, no behavior in C++
 * beyond applying these rules.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLD_API UWorldHub_SavePartitionPolicyDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Ordered partition definitions; the first match wins for a given slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	TArray<FWorldHub_SavePartitionDef> Partitions;

	/** Key-rename migrations applied to a loaded partition snapshot before merge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|SavePartition")
	TArray<FWorldHub_KeyMigration> KeyMigrations;

	/** Find a partition definition by id, or null. */
	const FWorldHub_SavePartitionDef* FindPartition(const FGameplayTag& PartitionId) const
	{
		for (const FWorldHub_SavePartitionDef& Def : Partitions)
		{
			if (Def.PartitionId == PartitionId)
			{
				return &Def;
			}
		}
		return nullptr;
	}
};
