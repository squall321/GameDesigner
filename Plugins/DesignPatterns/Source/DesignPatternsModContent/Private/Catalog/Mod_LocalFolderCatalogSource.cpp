// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Catalog/Mod_LocalFolderCatalogSource.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

// =====================================================================================================
// Configuration / scan
// =====================================================================================================

void UMod_LocalFolderCatalogSource::Configure(const FString& StoreFolder, const FString& SandboxedDestination, FGameplayTag InSourceId)
{
	StoreFolderAbsolute = FPaths::ConvertRelativePathToFull(StoreFolder);
	DestinationFolderAbsolute = FPaths::ConvertRelativePathToFull(SandboxedDestination);
	SourceId = InSourceId.IsValid() ? InSourceId : ModTags::Source;
	RescanStore();
}

FGameplayTag UMod_LocalFolderCatalogSource::MakeCatalogItemId(const FString& BaseName)
{
	// Build DP.Mod.Catalog.<SanitisedBaseName> as a request-native tag. Sanitise to a tag-legal token.
	FString Token = BaseName;
	Token.ReplaceInline(TEXT(" "), TEXT("_"));
	Token.ReplaceInline(TEXT("."), TEXT("_"));
	Token.ReplaceInline(TEXT("-"), TEXT("_"));
	const FString Full = FString::Printf(TEXT("DP.Mod.Catalog.%s"), *Token);
	return FGameplayTag::RequestGameplayTag(FName(*Full), /*ErrorIfNotFound*/ false);
}

int32 UMod_LocalFolderCatalogSource::RescanStore()
{
	CachedEntries.Reset();
	if (StoreFolderAbsolute.IsEmpty())
	{
		return 0;
	}

	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*StoreFolderAbsolute))
	{
		UE_LOG(LogDP, Verbose, TEXT("ModContent: local catalog store folder '%s' does not exist."), *StoreFolderAbsolute);
		return 0;
	}

	// Treat .pak and .uplugin files in the store as listings.
	auto ScanPattern = [this, &FM](const TCHAR* Pattern)
	{
		TArray<FString> Found;
		FM.FindFiles(Found, *(StoreFolderAbsolute / Pattern), /*Files*/ true, /*Directories*/ false);
		for (const FString& File : Found)
		{
			const FString Base = FPaths::GetBaseFilename(File);
			const FGameplayTag ItemId = MakeCatalogItemId(Base);
			if (!ItemId.IsValid())
			{
				continue; // tag not registered in this project — skip rather than fabricate one
			}

			FMod_CatalogEntry Entry;
			Entry.CatalogItemId = ItemId;
			Entry.DisplayName = FText::FromString(Base);
			// Installed state mirrors whether a same-named file already sits in the destination.
			const FString DestPath = DestinationFolderAbsolute / File;
			Entry.State = (!DestinationFolderAbsolute.IsEmpty() && FM.FileExists(*DestPath))
				? EMod_CatalogItemState::Installed
				: EMod_CatalogItemState::NotInstalled;
			CachedEntries.Add(MoveTemp(Entry));
		}
	};

	ScanPattern(TEXT("*.pak"));
	ScanPattern(TEXT("*.uplugin"));

	return CachedEntries.Num();
}

const FMod_CatalogEntry* UMod_LocalFolderCatalogSource::FindEntry(FGameplayTag CatalogItemId) const
{
	return CachedEntries.FindByPredicate([CatalogItemId](const FMod_CatalogEntry& E) { return E.CatalogItemId == CatalogItemId; });
}

// =====================================================================================================
// ISeam_ModCatalogSource
// =====================================================================================================

int32 UMod_LocalFolderCatalogSource::EnumerateCatalog_Implementation(TArray<FMod_CatalogEntry>& Out) const
{
	Out.Append(CachedEntries);
	return CachedEntries.Num();
}

bool UMod_LocalFolderCatalogSource::RequestDownload_Implementation(FGameplayTag CatalogItemId)
{
	if (!IsConfigured())
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: catalog download refused — source not configured (no store/destination)."));
		return false;
	}

	const FMod_CatalogEntry* Entry = FindEntry(CatalogItemId);
	if (!Entry)
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: catalog download refused — unknown item '%s'."), *CatalogItemId.ToString());
		return false;
	}

	IFileManager& FM = IFileManager::Get();

	// Find the actual store file for this listing (re-derive the file name from the base name).
	const FString BaseName = Entry->DisplayName.ToString();

	// Locate the source file (.pak or .uplugin) by base name in the store.
	FString SourceFile;
	for (const TCHAR* Pattern : { TEXT("*.pak"), TEXT("*.uplugin") })
	{
		TArray<FString> Found;
		FM.FindFiles(Found, *(StoreFolderAbsolute / Pattern), /*Files*/ true, /*Directories*/ false);
		for (const FString& File : Found)
		{
			if (FPaths::GetBaseFilename(File) == BaseName)
			{
				SourceFile = File;
				break;
			}
		}
		if (!SourceFile.IsEmpty()) { break; }
	}

	if (SourceFile.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: catalog download refused — store file for '%s' vanished."), *CatalogItemId.ToString());
		return false;
	}

	const FString AbsSource = StoreFolderAbsolute / SourceFile;
	const FString AbsDest = DestinationFolderAbsolute / SourceFile;

	// SANDBOX GUARD: confirm the resolved destination really sits under the destination root (defence
	// against a crafted file name containing path-traversal segments). A copy is never executed.
	const FString FullDest = FPaths::ConvertRelativePathToFull(AbsDest);
	const FString FullDestRoot = FPaths::ConvertRelativePathToFull(DestinationFolderAbsolute);
	if (!FullDest.StartsWith(FullDestRoot))
	{
		UE_LOG(LogDP, Error, TEXT("ModContent: catalog download REJECTED — destination '%s' escapes sandbox '%s'."),
			*FullDest, *FullDestRoot);
		return false;
	}

	// Ensure the destination directory exists, then copy (content only — never run anything).
	FM.MakeDirectory(*DestinationFolderAbsolute, /*Tree*/ true);
	const uint32 CopyResult = FM.Copy(*AbsDest, *AbsSource, /*bReplace*/ true, /*bEvenIfReadOnly*/ false);
	const bool bOk = (CopyResult == COPY_OK);

	UE_LOG(LogDP, Log, TEXT("ModContent: catalog download of '%s' -> %s (%s)."),
		*CatalogItemId.ToString(), *AbsDest, bOk ? TEXT("ok") : TEXT("FAILED"));

	// Refresh cached state so a subsequent GetCatalogState reflects the install.
	if (bOk)
	{
		RescanStore();
	}
	return bOk;
}

EMod_CatalogItemState UMod_LocalFolderCatalogSource::GetCatalogState_Implementation(FGameplayTag CatalogItemId) const
{
	if (const FMod_CatalogEntry* Entry = FindEntry(CatalogItemId))
	{
		return Entry->State;
	}
	return EMod_CatalogItemState::NotInstalled;
}

FGameplayTag UMod_LocalFolderCatalogSource::GetSourceId_Implementation() const
{
	return SourceId.IsValid() ? SourceId : ModTags::Source;
}

// =====================================================================================================
// IMod_ContentSource — report packs installed into the destination (discovery directory)
// =====================================================================================================

int32 UMod_LocalFolderCatalogSource::EnumeratePacks_Implementation(TArray<FMod_PackInfo>& OutPacks)
{
	// This source does NOT itself construct full FMod_PackInfo records from disk — that responsibility
	// already belongs to the manager's GatherFromDirectories scan over DiscoveryDirectories (which this
	// source copies into). Reporting nothing here avoids duplicating discovery while still letting the
	// object satisfy the IMod_ContentSource seam (so a project can register one object as both a catalog
	// bridge and a content source). The manager's disk discovery picks up downloaded packs normally.
	return 0;
}
