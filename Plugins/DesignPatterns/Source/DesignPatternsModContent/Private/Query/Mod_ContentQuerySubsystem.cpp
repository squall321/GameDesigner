// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/Mod_ContentQuerySubsystem.h"
#include "Manager/Mod_ContentManagerSubsystem.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Engine/GameInstance.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Misc/Paths.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_ContentQuerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Verbose, TEXT("ModContent: content query subsystem initialised."));
}

void UMod_ContentQuerySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

// =====================================================================================================
// Queries
// =====================================================================================================

TArray<FGameplayTag> UMod_ContentQuerySubsystem::GetPacksProvidingTag(FGameplayTag DataTag) const
{
	TArray<FGameplayTag> Out;
	if (const UMod_ContentRegistrySubsystem* Registry = ResolveRegistry())
	{
		for (const FMod_AssetOverride& Ov : Registry->GetOverridesForTag(DataTag))
		{
			Out.AddUnique(Ov.SourcePackId); // already precedence-sorted; AddUnique preserves order
		}
	}
	return Out;
}

void UMod_ContentQuerySubsystem::GatherPackPathPrefixes(const FMod_PackInfo& Info, TArray<FString>& OutPrefixes)
{
	OutPrefixes.Reset();

	// Plugin packs expose a /PluginName/ virtual root.
	if (Info.Kind == EMod_PackKind::Plugin && !Info.PluginName.IsEmpty())
	{
		OutPrefixes.AddUnique(FString::Printf(TEXT("/%s/"), *Info.PluginName));
	}

	// Descriptor content roots (authoritative for pak packs, informational for plugins).
	if (const UMod_ContentPackDescriptor* Desc = Info.Descriptor.Get())
	{
		for (const FString& Root : Desc->ContentRoots)
		{
			if (!Root.IsEmpty())
			{
				FString Norm = Root;
				if (!Norm.StartsWith(TEXT("/"))) { Norm = TEXT("/") + Norm; }
				if (!Norm.EndsWith(TEXT("/"))) { Norm += TEXT("/"); }
				OutPrefixes.AddUnique(Norm);
			}
		}
	}
}

FGameplayTag UMod_ContentQuerySubsystem::GetAssetOriginPack(const FSoftObjectPath& AssetPath) const
{
	const FString PackageName = AssetPath.GetLongPackageName();
	if (PackageName.IsEmpty())
	{
		return FGameplayTag();
	}

	const UMod_ContentManagerSubsystem* Manager = ResolveManager();
	if (!Manager)
	{
		return FGameplayTag();
	}

	// Match against the LONGEST prefix across all mounted packs so a nested root wins over a parent root.
	FGameplayTag BestPack;
	int32 BestPrefixLen = 0;

	for (const FMod_MountedPack& Rec : Manager->GetMountedPacks())
	{
		TArray<FString> Prefixes;
		GatherPackPathPrefixes(Rec.Info, Prefixes);
		for (const FString& Prefix : Prefixes)
		{
			if (PackageName.StartsWith(Prefix) && Prefix.Len() > BestPrefixLen)
			{
				BestPrefixLen = Prefix.Len();
				BestPack = Rec.Info.PackId;
			}
		}
	}

	return BestPack;
}

TArray<FMod_AssetOverride> UMod_ContentQuerySubsystem::GetOverrideChain(FGameplayTag DataTag) const
{
	if (const UMod_ContentRegistrySubsystem* Registry = ResolveRegistry())
	{
		return Registry->GetOverridesForTag(DataTag);
	}
	return TArray<FMod_AssetOverride>();
}

TArray<FSoftObjectPath> UMod_ContentQuerySubsystem::FindAssetsFromPack(FGameplayTag PackId) const
{
	TArray<FSoftObjectPath> Out;

	const UMod_ContentManagerSubsystem* Manager = ResolveManager();
	if (!Manager)
	{
		return Out;
	}

	FMod_MountedPack Rec;
	if (!Manager->GetPackRecord(PackId, Rec))
	{
		return Out;
	}

	TArray<FString> Prefixes;
	GatherPackPathPrefixes(Rec.Info, Prefixes);
	if (Prefixes.Num() == 0)
	{
		return Out;
	}

	const IAssetRegistry& AR = FAssetRegistryModule::GetRegistry();

	for (const FString& Prefix : Prefixes)
	{
		// Trim the trailing slash to form an asset-registry package PATH for recursive enumeration.
		FString PackagePath = Prefix;
		PackagePath.RemoveFromEnd(TEXT("/"));

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(*PackagePath));

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);
		for (const FAssetData& Data : Assets)
		{
			Out.AddUnique(Data.ToSoftObjectPath());
		}
	}

	return Out;
}

// =====================================================================================================
// Resolution / debug
// =====================================================================================================

UMod_ContentManagerSubsystem* UMod_ContentQuerySubsystem::ResolveManager() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UMod_ContentManagerSubsystem>();
	}
	return nullptr;
}

UMod_ContentRegistrySubsystem* UMod_ContentQuerySubsystem::ResolveRegistry() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UMod_ContentRegistrySubsystem>();
	}
	return nullptr;
}

FString UMod_ContentQuerySubsystem::GetDPDebugString_Implementation() const
{
	int32 Overridden = 0;
	if (const UMod_ContentRegistrySubsystem* Registry = ResolveRegistry())
	{
		Overridden = Registry->NumOverrides();
	}
	return FString::Printf(TEXT("ContentQuery: %d overridden tag(s) inspectable"), Overridden);
}
