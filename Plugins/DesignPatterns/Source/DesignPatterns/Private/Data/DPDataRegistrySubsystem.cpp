// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPLog.h"
#include "Core/DPDeveloperSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Stats/Stats.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Data Registry Indexed Tags"), STAT_DPDataIndexed, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Data Registry Build Index"), STAT_DPDataBuildIndex, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Data Registry Find By Tag"), STAT_DPDataFindByTag, STATGROUP_DesignPatterns);

namespace
{
	/** Name of the AssetRegistrySearchable property on UDP_DataAsset we read tag identity from. */
	const FName GDataTagPropertyName(TEXT("DataTag"));
}

void UDP_DataRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLoggingByDefault;
	}

	// Subscribe to AssetRegistry change delegates so the index self-heals. We do NOT force a
	// synchronous scan here — building is lazy on first query (and after FilesLoaded).
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	OnAssetAddedHandle   = AssetRegistry.OnAssetAdded().AddUObject(this, &UDP_DataRegistrySubsystem::HandleAssetAdded);
	OnAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddUObject(this, &UDP_DataRegistrySubsystem::HandleAssetRemoved);
	OnAssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddUObject(this, &UDP_DataRegistrySubsystem::HandleAssetRenamed);
	OnAssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddUObject(this, &UDP_DataRegistrySubsystem::HandleAssetUpdated);

	// If the registry is still discovering files, rebuild once it finishes so the index is complete.
	if (AssetRegistry.IsLoadingAssets())
	{
		OnFilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddUObject(this, &UDP_DataRegistrySubsystem::HandleFilesLoaded);
	}

	UE_LOG(LogDPData, Verbose, TEXT("DataRegistry initialized; index will build lazily on first query."));
}

void UDP_DataRegistrySubsystem::Deinitialize()
{
	if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = Module->Get();
		AssetRegistry.OnAssetAdded().Remove(OnAssetAddedHandle);
		AssetRegistry.OnAssetRemoved().Remove(OnAssetRemovedHandle);
		AssetRegistry.OnAssetRenamed().Remove(OnAssetRenamedHandle);
		AssetRegistry.OnAssetUpdated().Remove(OnAssetUpdatedHandle);
		if (OnFilesLoadedHandle.IsValid())
		{
			AssetRegistry.OnFilesLoaded().Remove(OnFilesLoadedHandle);
		}
	}
	OnAssetAddedHandle.Reset();
	OnAssetRemovedHandle.Reset();
	OnAssetRenamedHandle.Reset();
	OnAssetUpdatedHandle.Reset();
	OnFilesLoadedHandle.Reset();

	Index.Reset();
	bIndexBuilt = false;

	Super::Deinitialize();
}

void UDP_DataRegistrySubsystem::RefreshRelevantClassPaths()
{
	RelevantClassPaths.Reset();

	const FTopLevelAssetPath BasePath = UDP_DataAsset::StaticClass()->GetClassPathName();
	RelevantClassPaths.Add(BasePath);

	// Ask the AssetRegistry for every class derived from UDP_DataAsset (native + Blueprint),
	// without loading any of them.
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TSet<FTopLevelAssetPath> Derived;
	TArray<FTopLevelAssetPath> BaseNames = { BasePath };
	const TSet<FTopLevelAssetPath> Excluded;
	AssetRegistry.GetDerivedClassNames(BaseNames, Excluded, Derived);
	RelevantClassPaths.Append(Derived);
}

void UDP_DataRegistrySubsystem::EnsureIndexBuilt()
{
	if (!bIndexBuilt)
	{
		BuildIndexInternal();
	}
}

bool UDP_DataRegistrySubsystem::ExtractDataTag(const FAssetData& AssetData, FGameplayTag& OutTag)
{
	// AssetRegistrySearchable FGameplayTag is serialized into the tag map as its string form,
	// e.g. (TagName="DP.Data.Weapon.Sword"). Read it without loading the package.
	FString RawValue;
	if (!AssetData.GetTagValue(GDataTagPropertyName, RawValue) || RawValue.IsEmpty())
	{
		return false;
	}

	// Strip the FGameplayTag export wrapper if present: (TagName="X") -> X.
	FString TagString = RawValue;
	const int32 QuoteStart = TagString.Find(TEXT("\""));
	if (QuoteStart != INDEX_NONE)
	{
		const int32 QuoteEnd = TagString.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, QuoteStart + 1);
		if (QuoteEnd != INDEX_NONE && QuoteEnd > QuoteStart)
		{
			TagString = TagString.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}
	}

	TagString.TrimStartAndEndInline();
	if (TagString.IsEmpty() || TagString == TEXT("None"))
	{
		return false;
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagString), /*ErrorIfNotFound*/ false);
	if (!Tag.IsValid())
	{
		return false;
	}
	OutTag = Tag;
	return true;
}

bool UDP_DataRegistrySubsystem::IsRelevantAsset(const FAssetData& AssetData) const
{
	// Class gate (load-free): test the asset's class path against the precomputed set of
	// class names derived from UDP_DataAsset. Never forces the class to load.
	if (!RelevantClassPaths.Contains(AssetData.AssetClassPath))
	{
		return false;
	}

	// Path gate: empty scan-path list means "everywhere".
	const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get();
	if (!Settings || Settings->DataRegistryScanPaths.Num() == 0)
	{
		return true;
	}

	const FString PackagePath = AssetData.PackageName.ToString();
	for (const FDirectoryPath& Dir : Settings->DataRegistryScanPaths)
	{
		if (!Dir.Path.IsEmpty() && PackagePath.StartsWith(Dir.Path))
		{
			return true;
		}
	}
	return false;
}

void UDP_DataRegistrySubsystem::IndexAsset(const FAssetData& AssetData)
{
	if (!IsRelevantAsset(AssetData))
	{
		return;
	}

	FGameplayTag Tag;
	if (!ExtractDataTag(AssetData, Tag))
	{
		UE_CLOG(bEnableVerboseLogging, LogDPData, Verbose,
			TEXT("Skipping '%s': no valid DataTag in asset registry tags."), *AssetData.GetObjectPathString());
		return;
	}

	if (FEntry* Existing = Index.Find(Tag))
	{
		if (Existing->AssetPath != AssetData.GetSoftObjectPath())
		{
			UE_LOG(LogDPData, Warning,
				TEXT("Duplicate DataTag '%s': '%s' collides with already-indexed '%s'. Keeping the first."),
				*Tag.ToString(), *AssetData.GetObjectPathString(), *Existing->AssetPath.ToString());
		}
		return;
	}

	FEntry NewEntry;
	NewEntry.AssetId = AssetData.GetPrimaryAssetId();
	NewEntry.AssetPath = AssetData.GetSoftObjectPath();
	Index.Add(Tag, MoveTemp(NewEntry));
}

void UDP_DataRegistrySubsystem::BuildIndexInternal()
{
	SCOPE_CYCLE_COUNTER(STAT_DPDataBuildIndex);

	Index.Reset();
	RefreshRelevantClassPaths();

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UDP_DataAsset::StaticClass()->GetClassPathName());

	// Restrict the scan to configured paths when present (more efficient than filtering after).
	const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get();
	if (Settings)
	{
		for (const FDirectoryPath& Dir : Settings->DataRegistryScanPaths)
		{
			if (!Dir.Path.IsEmpty())
			{
				Filter.PackagePaths.Add(FName(*Dir.Path));
			}
		}
	}
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssets(Filter, Assets);

	for (const FAssetData& AssetData : Assets)
	{
		IndexAsset(AssetData);
	}

	bIndexBuilt = true;
	SET_DWORD_STAT(STAT_DPDataIndexed, Index.Num());
	UE_LOG(LogDPData, Log, TEXT("DataRegistry built index: %d tag(s) from %d candidate asset(s)."),
		Index.Num(), Assets.Num());
}

void UDP_DataRegistrySubsystem::RebuildIndex()
{
	BuildIndexInternal();
}

UDP_DataAsset* UDP_DataRegistrySubsystem::FindByTag(FGameplayTag Tag)
{
	SCOPE_CYCLE_COUNTER(STAT_DPDataFindByTag);

	if (!Tag.IsValid())
	{
		return nullptr;
	}
	EnsureIndexBuilt();

	FEntry* Entry = Index.Find(Tag);
	if (!Entry)
	{
		UE_CLOG(bEnableVerboseLogging, LogDPData, Verbose, TEXT("FindByTag: no asset for tag '%s'."), *Tag.ToString());
		return nullptr;
	}

	// Return the cached load if still alive.
	if (UDP_DataAsset* Cached = Entry->Loaded.Get())
	{
		return Cached;
	}

	// Synchronous load on first resolution. The index itself stayed load-free.
	UDP_DataAsset* Loaded = Cast<UDP_DataAsset>(Entry->AssetPath.TryLoad());
	if (!Loaded)
	{
		UE_LOG(LogDPData, Warning, TEXT("FindByTag: failed to load asset '%s' for tag '%s'."),
			*Entry->AssetPath.ToString(), *Tag.ToString());
		return nullptr;
	}
	Entry->Loaded = Loaded;
	return Loaded;
}

FPrimaryAssetId UDP_DataRegistrySubsystem::ResolveAssetId(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return FPrimaryAssetId();
	}
	// const path: build lazily without mutating cached-load state via a const_cast on the gate.
	const_cast<UDP_DataRegistrySubsystem*>(this)->EnsureIndexBuilt();
	if (const FEntry* Entry = Index.Find(Tag))
	{
		return Entry->AssetId;
	}
	return FPrimaryAssetId();
}

bool UDP_DataRegistrySubsystem::Contains(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return false;
	}
	const_cast<UDP_DataRegistrySubsystem*>(this)->EnsureIndexBuilt();
	return Index.Contains(Tag);
}

TArray<FGameplayTag> UDP_DataRegistrySubsystem::ListTags() const
{
	const_cast<UDP_DataRegistrySubsystem*>(this)->EnsureIndexBuilt();
	TArray<FGameplayTag> Tags;
	Index.GenerateKeyArray(Tags);
	return Tags;
}

int32 UDP_DataRegistrySubsystem::Num() const
{
	const_cast<UDP_DataRegistrySubsystem*>(this)->EnsureIndexBuilt();
	return Index.Num();
}

FString UDP_DataRegistrySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("DataRegistry: %d indexed tag(s) (built=%s)"),
		Index.Num(), bIndexBuilt ? TEXT("yes") : TEXT("no"));
}

// ---------------------------------------------------------------------------------------
// Self-heal callbacks
// ---------------------------------------------------------------------------------------

void UDP_DataRegistrySubsystem::HandleAssetAdded(const FAssetData& AssetData)
{
	if (!bIndexBuilt)
	{
		// Index not yet built; it will pick this asset up when built lazily.
		return;
	}
	IndexAsset(AssetData);
	SET_DWORD_STAT(STAT_DPDataIndexed, Index.Num());
}

void UDP_DataRegistrySubsystem::HandleAssetRemoved(const FAssetData& AssetData)
{
	if (!bIndexBuilt || !IsRelevantAsset(AssetData))
	{
		return;
	}
	const FSoftObjectPath RemovedPath = AssetData.GetSoftObjectPath();
	for (auto It = Index.CreateIterator(); It; ++It)
	{
		if (It.Value().AssetPath == RemovedPath)
		{
			UE_CLOG(bEnableVerboseLogging, LogDPData, Verbose,
				TEXT("Removed tag '%s' (asset deleted)."), *It.Key().ToString());
			It.RemoveCurrent();
		}
	}
	SET_DWORD_STAT(STAT_DPDataIndexed, Index.Num());
}

void UDP_DataRegistrySubsystem::HandleAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	if (!bIndexBuilt)
	{
		return;
	}
	// Drop any entry pointing at the old path, then re-index the asset at its new path.
	const FSoftObjectPath OldPath(OldObjectPath);
	for (auto It = Index.CreateIterator(); It; ++It)
	{
		if (It.Value().AssetPath == OldPath)
		{
			It.RemoveCurrent();
		}
	}
	IndexAsset(AssetData);
	SET_DWORD_STAT(STAT_DPDataIndexed, Index.Num());
}

void UDP_DataRegistrySubsystem::HandleAssetUpdated(const FAssetData& AssetData)
{
	if (!bIndexBuilt || !IsRelevantAsset(AssetData))
	{
		return;
	}
	// The DataTag may have changed: drop stale entries for this path, then re-index.
	const FSoftObjectPath UpdatedPath = AssetData.GetSoftObjectPath();
	for (auto It = Index.CreateIterator(); It; ++It)
	{
		if (It.Value().AssetPath == UpdatedPath)
		{
			It.RemoveCurrent();
		}
	}
	IndexAsset(AssetData);
	SET_DWORD_STAT(STAT_DPDataIndexed, Index.Num());
}

void UDP_DataRegistrySubsystem::HandleFilesLoaded()
{
	// First full discovery finished — rebuild so any assets missed during a partial scan are present.
	UE_LOG(LogDPData, Verbose, TEXT("AssetRegistry finished loading; rebuilding DataRegistry index."));
	BuildIndexInternal();
	OnFilesLoadedHandle.Reset();
}
