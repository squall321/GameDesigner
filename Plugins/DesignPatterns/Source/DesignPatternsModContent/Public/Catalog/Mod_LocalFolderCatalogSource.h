// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Seam/Mod_ContentSource.h"          // IMod_ContentSource, FMod_PackInfo
#include "Mod/Seam_ModCatalog.h"            // ISeam_ModCatalogSource, FMod_CatalogEntry
#include "Mod_LocalFolderCatalogSource.generated.h"

/**
 * DEFAULT, network-free catalog/store bridge: it treats a configured local folder (the
 * UMod_DeveloperSettings::LocalCatalogFolder) as a "store", and a "download" is a SANDBOX-CONSTRAINED
 * file copy INTO a configured discovery directory — where the manager's normal disk discovery then sees
 * the pack. It NEVER mounts or executes anything; activation remains the manager's validate-before-mount
 * decision over already-sandboxed content.
 *
 * It implements BOTH:
 *   - ISeam_ModCatalogSource: enumerate listings, request a download, query item state.
 *   - IMod_ContentSource: so the same object can also report the packs it has installed into the
 *     discovery directory, letting the manager discover them through the normal source path.
 *
 * LIFETIME: this is a plain UObject created with NewObject (no auto-GC root). It MUST be kept alive by a
 * strong owner — UMod_CatalogBridgeSubsystem holds it in a UPROPERTY TObjectPtr — and is only THEN
 * registered with the locator / manager as a WEAK reference. A bare NewObject without a strong owner
 * would be garbage-collected.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSMODCONTENT_API UMod_LocalFolderCatalogSource
	: public UObject
	, public ISeam_ModCatalogSource
	, public IMod_ContentSource
{
	GENERATED_BODY()

public:
	/**
	 * Configure the source. StoreFolder is the absolute "store" folder; SandboxedDestination is the
	 * absolute discovery directory a download copies into (MUST be a configured DiscoveryDirectory so the
	 * destination is inside the sandbox). InSourceId is this source's provenance id (child of DP.Mod.Source).
	 */
	void Configure(const FString& StoreFolder, const FString& SandboxedDestination, FGameplayTag InSourceId);

	/** Re-scan the store folder, rebuilding the cached catalog entries. Game-thread, load-free. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Catalog")
	int32 RescanStore();

	//~ Begin ISeam_ModCatalogSource
	virtual int32 EnumerateCatalog_Implementation(TArray<FMod_CatalogEntry>& Out) const override;
	virtual bool RequestDownload_Implementation(FGameplayTag CatalogItemId) override;
	virtual EMod_CatalogItemState GetCatalogState_Implementation(FGameplayTag CatalogItemId) const override;
	//~ End ISeam_ModCatalogSource

	//~ Begin IMod_ContentSource
	virtual int32 EnumeratePacks_Implementation(TArray<FMod_PackInfo>& OutPacks) override;
	//~ End IMod_ContentSource

	/**
	 * Shared GetSourceId_Implementation: BOTH ISeam_ModCatalogSource AND IMod_ContentSource declare a
	 * BlueprintNativeEvent FGameplayTag GetSourceId() const with the SAME signature, so a single override
	 * satisfies both interfaces' Execute_GetSourceId thunks.
	 */
	virtual FGameplayTag GetSourceId_Implementation() const override;

private:
	/** The absolute "store" folder this source enumerates as its catalog. */
	UPROPERTY(Transient)
	FString StoreFolderAbsolute;

	/** The absolute discovery directory a download copies INTO (sandbox-constrained). */
	UPROPERTY(Transient)
	FString DestinationFolderAbsolute;

	/** This source's provenance id (child of DP.Mod.Source). */
	UPROPERTY(Transient)
	FGameplayTag SourceId;

	/** Cached catalog entries from the last RescanStore. Keyed by catalog item id. */
	UPROPERTY(Transient)
	TArray<FMod_CatalogEntry> CachedEntries;

	/** True if Destination resolves under Store or vice-versa is irrelevant; we only require Destination
	 *  to be the (already sandbox-validated) discovery directory the bridge passed in. */
	bool IsConfigured() const { return !StoreFolderAbsolute.IsEmpty() && !DestinationFolderAbsolute.IsEmpty(); }

	/** Find a cached entry by id (const). Null when absent. */
	const FMod_CatalogEntry* FindEntry(FGameplayTag CatalogItemId) const;

	/** Make a catalog item id (child of DP.Mod.Catalog) from a store file's base name. */
	static FGameplayTag MakeCatalogItemId(const FString& BaseName);
};
