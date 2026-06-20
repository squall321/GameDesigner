// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Registry/WorldHub_FlagTypes.h"
#include "WorldHub_FlagSetDataAsset.generated.h"

/**
 * A data-authored set of world-hub flag definitions.
 *
 * A flag set groups related flags/variables/counters (e.g. one set per quest line, one per
 * game-wide settings family) so the hub can load them by DataTag through the core data registry
 * and initialize every defined slot with its default, replication and save policy. There is no
 * hardcoded flag in C++; all tunable identity, defaults and bounds live in these assets.
 *
 * Identity is the inherited DataTag (e.g. DP.Data.WorldHub.FlagSet.Tutorial). The hub resolves
 * sets by DataTag and indexes their definitions by each FWorldHub_FlagDefinition::Key.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLD_API UWorldHub_FlagSetDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UWorldHub_FlagSetDataAsset();

	/**
	 * The flag definitions contributed by this set. Each Key must be unique within the set
	 * (duplicates are flagged by editor validation and the last duplicate wins at runtime with a
	 * warning).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (TitleProperty = "Key"), Category = "DesignPatterns|WorldHub|FlagSet")
	TArray<FWorldHub_FlagDefinition> Definitions;

	/**
	 * Look up a definition in this set by its key.
	 * @return a pointer to the definition, or nullptr if no definition has that key.
	 */
	const FWorldHub_FlagDefinition* FindDefinition(const FGameplayTag& Key) const;

	/** Blueprint-friendly variant of FindDefinition. @return true (and fills Out) when found. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|FlagSet")
	bool FindDefinitionByKey(const FGameplayTag& Key, FWorldHub_FlagDefinition& OutDefinition) const;

	/** @return every key defined in this set, in declaration order. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|FlagSet")
	void GetAllKeys(TArray<FGameplayTag>& OutKeys) const;

	//~ Begin UDP_DataAsset
	/** Groups all flag sets under one asset-manager type bucket so they scan together. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags empty keys and duplicate keys within the set, and bad counter bounds. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
