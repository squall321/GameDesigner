// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Registry/Mod_ContentRegistrySubsystem.h"   // FMod_AssetOverride (return type)
#include "Mod_ContentQuerySubsystem.generated.h"

class UMod_ContentManagerSubsystem;

/**
 * READ-ONLY content query / asset-origin / override-chain inspector.
 *
 * It answers "which packs provide tag X?", "which pack does this asset come from?", and "what is the
 * override chain for this data tag?" — the questions a load-order UI, a debug inspector, or save-migration
 * tooling asks. It is purely additive and load-free: it reads the registry ONLY through its public API
 * (including the new additive const accessors GetOverridesForTag / GetAllOverrides — never private state)
 * and wraps the engine AssetRegistry to map a soft object path to its providing pack by sandbox-root /
 * mount-point prefix.
 *
 * GameInstance-scoped, non-replicated, non-saved. Resolves siblings (manager / registry) by GI lookup
 * each call so it never pins a stale subsystem.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_ContentQuerySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Which mounted packs currently provide (override) a data tag, in descending precedence (element 0 is
	 * the current winner). Empty when no pack overrides the tag. Reads the registry's public override chain.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Query")
	TArray<FGameplayTag> GetPacksProvidingTag(FGameplayTag DataTag) const;

	/**
	 * The pack a given asset originates from, determined by matching the asset's package path against each
	 * mounted pack's mount point / content roots. Returns an invalid tag when the asset belongs to base
	 * (non-pack) content or no pack matches. Load-free (path prefix match only).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Query")
	FGameplayTag GetAssetOriginPack(const FSoftObjectPath& AssetPath) const;

	/**
	 * The full override CHAIN for a data tag (every contributing override across all packs, precedence
	 * sorted). Element 0 is the winner. Empty when unoverridden. A copy of the registry's data — additive.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Query")
	TArray<FMod_AssetOverride> GetOverrideChain(FGameplayTag DataTag) const;

	/**
	 * Every asset (by soft path) that lives under PackId's mount point / content roots, via the engine
	 * AssetRegistry. Load-free (asset-data enumeration only). Empty when the pack is unknown/unmounted or
	 * the AssetRegistry has not yet scanned its roots.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Query")
	TArray<FSoftObjectPath> FindAssetsFromPack(FGameplayTag PackId) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the manager subsystem (GI sibling). Null in early/teardown contexts. */
	UMod_ContentManagerSubsystem* ResolveManager() const;

	/** Resolve the mod-aware registry subsystem (GI sibling). Null in early/teardown contexts. */
	UMod_ContentRegistrySubsystem* ResolveRegistry() const;

	/**
	 * Compute the set of virtual package-path prefixes a pack contributes (mount point + descriptor
	 * content roots), used for asset-origin matching. Normalised to "/Prefix/" form.
	 */
	static void GatherPackPathPrefixes(const struct FMod_PackInfo& Info, TArray<FString>& OutPrefixes);
};
