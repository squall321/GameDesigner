// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Catalog/Mod_CatalogBridgeSubsystem.h"
#include "Catalog/Mod_LocalFolderCatalogSource.h"
#include "Settings/Mod_DeveloperSettings.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_CatalogBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	RebuildDefaultSource();
}

void UMod_CatalogBridgeSubsystem::Deinitialize()
{
	if (bRegistered)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
		{
			Locator->UnregisterService(ModTags::Service_ModCatalog);
		}
		bRegistered = false;
	}
	DefaultSource = nullptr; // drop the strong owner so it can be GC'd
	Super::Deinitialize();
}

// =====================================================================================================
// Build / register the default source
// =====================================================================================================

void UMod_CatalogBridgeSubsystem::RebuildDefaultSource()
{
	const UMod_DeveloperSettings* Settings = UMod_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	// Resolve the "store" folder (LocalCatalogFolder). Empty/unset -> inert (no default catalog).
	const FString StoreRel = Settings->LocalCatalogFolder.Path;
	if (StoreRel.IsEmpty())
	{
		UE_LOG(LogDP, Verbose, TEXT("ModContent: no LocalCatalogFolder configured; default catalog source inert."));
		return;
	}
	const FString StoreAbs = FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(StoreRel) ? (FPaths::ProjectDir() / StoreRel) : StoreRel);

	// Resolve the sandboxed download destination: a chosen discovery directory (so the destination is a
	// path the manager already treats as sandbox-safe). Clamp the index defensively.
	if (Settings->DiscoveryDirectories.Num() == 0)
	{
		UE_LOG(LogDP, Verbose, TEXT("ModContent: no DiscoveryDirectories configured; default catalog cannot place downloads."));
		return;
	}
	const int32 Index = FMath::Clamp(Settings->CatalogDownloadDirectoryIndex, 0, Settings->DiscoveryDirectories.Num() - 1);
	const FString DestRel = Settings->DiscoveryDirectories[Index].Path;
	const FString DestAbs = FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(DestRel) ? (FPaths::ProjectDir() / DestRel) : DestRel);

	// (Re)create the strongly-owned source.
	if (!DefaultSource)
	{
		DefaultSource = NewObject<UMod_LocalFolderCatalogSource>(this);
	}

	// SourceId anchored under DP.Mod.Source.LocalCatalog (best-effort request; falls back to DP.Mod.Source).
	const FGameplayTag CatalogSourceId =
		FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Mod.Source.LocalCatalog")), /*ErrorIfNotFound*/ false);

	DefaultSource->Configure(StoreAbs, DestAbs, CatalogSourceId);

	// Register WEAKLY with the locator under the catalog service key (UI resolves the seam from here).
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(ModTags::Service_ModCatalog, DefaultSource, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
		bRegistered = true;
	}

	UE_LOG(LogDP, Log, TEXT("ModContent: default local-folder catalog source configured (store='%s', dest='%s')."),
		*StoreAbs, *DestAbs);
}

// =====================================================================================================
// Helpers / debug
// =====================================================================================================

UDP_ServiceLocatorSubsystem* UMod_CatalogBridgeSubsystem::GetLocator() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	}
	return nullptr;
}

FString UMod_CatalogBridgeSubsystem::GetDPDebugString_Implementation() const
{
	if (!DefaultSource)
	{
		return TEXT("CatalogBridge: inert (no default source)");
	}
	TArray<FMod_CatalogEntry> Entries;
	DefaultSource->EnumerateCatalog_Implementation(Entries);
	return FString::Printf(TEXT("CatalogBridge: %d listing(s), registered=%s"),
		Entries.Num(), bRegistered ? TEXT("yes") : TEXT("no"));
}
