// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DPDataAsset.generated.h"

/**
 * Base class for tag-identified data assets managed by the data registry.
 *
 * Identity is a single FGameplayTag (DataTag) rather than the asset's package path, so
 * game code resolves content by stable design-time meaning ("DP.Data.Weapon.Sword") instead
 * of by fragile object paths. The registry indexes these by DataTag straight from the
 * AssetRegistry (no asset load), so DataTag is surfaced as an asset-registry-searchable tag.
 *
 * Subclass this for each content family (items, enemies, levels...) and add your own fields.
 * The PrimaryAssetType groups assets for the engine's asset manager; override
 * GetDataAssetType() to give a family its own type bucket.
 */
UCLASS(BlueprintType, Abstract)
class DESIGNPATTERNS_API UDP_DataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Stable design-time identity. Must be unique across all DP data assets within a scan
	 * path; the registry warns on duplicates and IsDataValid flags them in the editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "DesignPatterns|Data")
	FGameplayTag DataTag;

	/** Optional human-readable label for tooling/debug UIs; not used for identity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Data")
	FText DisplayName;

	/**
	 * The PrimaryAssetType name used to build this asset's FPrimaryAssetId. Defaults to the
	 * concrete class name so each subclass forms its own asset-manager bucket. Override to
	 * collapse several subclasses into one shared type.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Data")
	FName GetDataAssetType() const;
	virtual FName GetDataAssetType_Implementation() const;

	//~ Begin UPrimaryDataAsset
	/** PrimaryAssetId is GetDataAssetType() : AssetName, so the asset manager can address it. */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags an empty DataTag (and is the hook the validator uses to surface duplicate tags). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
