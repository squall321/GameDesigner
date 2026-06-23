// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Mod_CatalogBridgeSubsystem.generated.h"

class UMod_LocalFolderCatalogSource;
class UDP_ServiceLocatorSubsystem;

/**
 * GameInstance subsystem that wires up the DEFAULT catalog/store bridge.
 *
 * On init it constructs (NewObject, outered to this subsystem) and STRONGLY OWNS a
 * UMod_LocalFolderCatalogSource in a UPROPERTY TObjectPtr (so it is GC-rooted), configures it from
 * UMod_DeveloperSettings (LocalCatalogFolder as the "store"; a chosen DiscoveryDirectory as the
 * sandboxed download destination), and registers it WEAKLY with the service locator under
 * DP.Service.Mod.Catalog. UI / tooling resolves the catalog seam from the locator.
 *
 * The bridge does NOT auto-mount anything: a download places a pack into the discovery directory; the
 * host must call DiscoverPacks()/MountPack() on the manager (validate-before-activate). When no
 * LocalCatalogFolder is configured (or it/the destination escapes the sandbox) the bridge stays inert.
 *
 * Non-replicated, non-saved.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_CatalogBridgeSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** The default local-folder catalog source this subsystem owns (may be null when not configured). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Catalog")
	UMod_LocalFolderCatalogSource* GetDefaultCatalogSource() const { return DefaultSource; }

	/** Re-read settings and (re)configure the default source. Safe to call at runtime. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Catalog")
	void RebuildDefaultSource();

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the service locator (may be null in early/teardown contexts). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/**
	 * Strong owner of the default catalog source. A bare NewObject without this UPROPERTY would be GC'd
	 * out from under the weakly-registered locator entry; holding it here keeps it alive for the GI's life.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UMod_LocalFolderCatalogSource> DefaultSource = nullptr;

	/** True once the default source was successfully configured and registered. */
	bool bRegistered = false;
};
