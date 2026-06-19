// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Engine/AssetManager.h"
#include "AssetRegistry/AssetData.h"
#include "Data/DPDataAsset.h"
#include "DPDataRegistrySubsystem.generated.h"

/**
 * Load-free index from FGameplayTag identity to the data asset that carries it.
 *
 * The index is built from the AssetRegistry alone — it reads the AssetRegistrySearchable
 * "DataTag" tag value off each asset's FAssetData WITHOUT loading the asset. Only when a
 * caller asks for the resolved UDP_DataAsset (FindByTag) is the package synchronously loaded.
 *
 * The index lazily builds on first use, then self-heals: it subscribes to AssetRegistry
 * add/remove/rename delegates so newly imported or deleted assets keep the map correct at
 * runtime and in-editor without a manual rebuild.
 *
 * Scan paths come from UDP_DeveloperSettings::DataRegistryScanPaths; an empty list means
 * "scan everywhere" (every UDP_DataAsset in the project).
 */
UCLASS()
class DESIGNPATTERNS_API UDP_DataRegistrySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Resolve and (synchronously) load the data asset registered under Tag.
	 * Returns nullptr if no asset carries that tag. Result is cached; repeat calls are cheap.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data", meta = (DisplayName = "Find Data Asset By Tag"))
	UDP_DataAsset* FindByTag(FGameplayTag Tag);

	/** Templated convenience: resolve by tag and cast to a concrete UDP_DataAsset subclass. */
	template <typename T>
	T* Find(const FGameplayTag& Tag)
	{
		static_assert(TIsDerivedFrom<T, UDP_DataAsset>::IsDerived, "T must derive from UDP_DataAsset");
		return Cast<T>(FindByTag(Tag));
	}

	/** Resolve a tag to its primary asset id WITHOUT loading the asset. Invalid if unknown. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data")
	FPrimaryAssetId ResolveAssetId(FGameplayTag Tag) const;

	/** True if a data asset is registered under Tag (no load). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data")
	bool Contains(FGameplayTag Tag) const;

	/** Every DataTag currently indexed. Order is unspecified. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data")
	TArray<FGameplayTag> ListTags() const;

	/** Number of indexed tags. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data")
	int32 Num() const;

	/** Force a full rebuild of the index from the AssetRegistry. Backing for DP.Data.RebuildIndex. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Data")
	void RebuildIndex();

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** One indexed entry: the resolved asset id plus a lazily-populated loaded pointer. */
	struct FEntry
	{
		FPrimaryAssetId AssetId;
		FSoftObjectPath AssetPath;
		TWeakObjectPtr<UDP_DataAsset> Loaded;
	};

	/** DataTag -> entry. */
	TMap<FGameplayTag, FEntry> Index;

	/** Load-free set of class path names that derive from UDP_DataAsset (incl. BP subclasses). */
	TSet<FTopLevelAssetPath> RelevantClassPaths;

	/** Whether the lazy index has been built at least once this session. */
	bool bIndexBuilt = false;

	/** AssetRegistry delegate handles, removed on Deinitialize. */
	FDelegateHandle OnAssetAddedHandle;
	FDelegateHandle OnAssetRemovedHandle;
	FDelegateHandle OnAssetRenamedHandle;
	FDelegateHandle OnAssetUpdatedHandle;
	FDelegateHandle OnFilesLoadedHandle;

	/** Build the index if it hasn't been built yet (lazy). */
	void EnsureIndexBuilt();

	/** Scan the AssetRegistry under the configured paths and (re)populate Index. */
	void BuildIndexInternal();

	/** (Re)compute RelevantClassPaths from the AssetRegistry class hierarchy (load-free). */
	void RefreshRelevantClassPaths();

	/** Extract the DataTag value from an FAssetData's searchable tags without loading it. */
	static bool ExtractDataTag(const FAssetData& AssetData, FGameplayTag& OutTag);

	/** True if AssetData is a UDP_DataAsset (or subclass) under a configured scan path. */
	bool IsRelevantAsset(const FAssetData& AssetData) const;

	/** Insert/refresh a single asset in the index. Used by add/update self-heal. */
	void IndexAsset(const FAssetData& AssetData);

	//~ AssetRegistry self-heal callbacks
	void HandleAssetAdded(const FAssetData& AssetData);
	void HandleAssetRemoved(const FAssetData& AssetData);
	void HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);
	void HandleAssetUpdated(const FAssetData& AssetData);
	void HandleFilesLoaded();
};
