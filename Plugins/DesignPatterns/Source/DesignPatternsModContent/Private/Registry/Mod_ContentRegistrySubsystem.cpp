// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Registry/Mod_ContentRegistrySubsystem.h"

#include "DesignPatternsModContentModule.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataAsset.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "NativeGameplayTags.h"
#include "Engine/AssetManager.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace ModRegistryTags
{
	// Owned by THIS (registry) area so it never collides with the manager-area tag definitions in the
	// module TU. Child of DP.Bus.Mod so a listener on DP.Bus.Mod (hierarchy match) also hears it.
	UE_DEFINE_GAMEPLAY_TAG(Bus_OverridesChanged, "DP.Bus.Mod.OverridesChanged");
}

void UMod_ContentRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Publish ourselves on the service locator so the manager and game code can resolve the override
	// front door by stable tag instead of by concrete class. WeakObserved: a GameInstance subsystem is
	// already kept alive by the engine, so the locator must only OBSERVE it (never hold a second strong
	// ref that could outlive the GI).
	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(ModTags::Service_ModContent, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}

	// Attempt to wire into the core registry's override-provider extension. If the running core build
	// has no such hook we fall back to front-door mode (callers must resolve through this subsystem).
	RegisterOverrideProvider();

	UE_LOG(LogDPData, Log, TEXT("[ModContent] Override registry initialized (core hook: %s)."),
		bWiredIntoCoreHook ? TEXT("installed") : TEXT("front-door mode"));
}

void UMod_ContentRegistrySubsystem::Deinitialize()
{
	UnregisterOverrideProvider();
	ClearAllOverrides();

	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only retract our own binding (a later override of the same key by someone else must win).
		if (Locator->Resolve<UMod_ContentRegistrySubsystem>(ModTags::Service_ModContent) == this)
		{
			Locator->UnregisterService(ModTags::Service_ModContent);
		}
	}

	CachedBaseRegistry = nullptr;
	Super::Deinitialize();
}

UDP_DataRegistrySubsystem* UMod_ContentRegistrySubsystem::ResolveBaseRegistry() const
{
	if (IsValid(CachedBaseRegistry))
	{
		return CachedBaseRegistry;
	}
	// Sibling GameInstance subsystem; engine owns its lifetime so a plain cached object ptr is fine.
	CachedBaseRegistry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	return CachedBaseRegistry;
}

FPrimaryAssetId UMod_ContentRegistrySubsystem::FindOverriddenAsset(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return FPrimaryAssetId();
	}

	// Override layer first.
	if (const FMod_AssetOverride* Winner = WinningByTag.Find(Tag))
	{
		if (Winner->AssetId.IsValid())
		{
			return Winner->AssetId;
		}
		// A malformed winner (invalid id) should not mask the base — log once and fall through.
		UE_LOG(LogDPData, Warning,
			TEXT("[ModContent] Override for tag '%s' (pack '%s') has an invalid AssetId; falling through to base."),
			*Tag.ToString(), *Winner->SourcePackId.ToString());
	}

	// Fall through to the base registry (load-free) when no pack overrides the tag.
	if (const UDP_DataRegistrySubsystem* Base = ResolveBaseRegistry())
	{
		return Base->ResolveAssetId(Tag);
	}
	return FPrimaryAssetId();
}

bool UMod_ContentRegistrySubsystem::HasOverride(FGameplayTag Tag) const
{
	return Tag.IsValid() && WinningByTag.Contains(Tag);
}

UDP_DataAsset* UMod_ContentRegistrySubsystem::ResolveAsset(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}

	// If a pack overrides the tag, synchronously load the overriding asset from its soft path. The path
	// is the authoritative locator we kept at apply time; using the asset manager id alone would not
	// honour a pack asset that the manager has not registered as a primary asset.
	if (const FMod_AssetOverride* Winner = WinningByTag.Find(Tag))
	{
		if (Winner->AssetPath.IsValid())
		{
			UObject* Loaded = Winner->AssetPath.TryLoad();
			if (UDP_DataAsset* AsData = Cast<UDP_DataAsset>(Loaded))
			{
				return AsData;
			}
			UE_LOG(LogDPData, Warning,
				TEXT("[ModContent] Override asset for tag '%s' (pack '%s') failed to load or is not a UDP_DataAsset (path '%s'); falling through."),
				*Tag.ToString(), *Winner->SourcePackId.ToString(), *Winner->AssetPath.ToString());
		}
	}

	// Fall through to the base registry's resolved-and-loaded asset.
	if (UDP_DataRegistrySubsystem* Base = ResolveBaseRegistry())
	{
		return Base->FindByTag(Tag);
	}
	return nullptr;
}

TArray<FGameplayTag> UMod_ContentRegistrySubsystem::ListOverriddenTags() const
{
	TArray<FGameplayTag> Out;
	WinningByTag.GetKeys(Out);
	return Out;
}

void UMod_ContentRegistrySubsystem::ApplyPackOverrides(FGameplayTag PackId, const TArray<FMod_AssetOverride>& Overrides)
{
	if (!PackId.IsValid())
	{
		UE_LOG(LogDPData, Warning, TEXT("[ModContent] ApplyPackOverrides called with an invalid pack id; ignored."));
		return;
	}

	// Record (or replace) this pack's contributions; remount is idempotent. Stamp the source pack id on
	// every entry defensively so a winner always knows which pack to drop on unmount.
	TArray<FMod_AssetOverride> Sanitized;
	Sanitized.Reserve(Overrides.Num());
	for (const FMod_AssetOverride& In : Overrides)
	{
		if (!In.DataTag.IsValid())
		{
			UE_LOG(LogDPData, Verbose,
				TEXT("[ModContent] Pack '%s' contributed an override with no DataTag; skipped."), *PackId.ToString());
			continue;
		}
		FMod_AssetOverride Entry = In;
		Entry.SourcePackId = PackId;
		Sanitized.Add(MoveTemp(Entry));
	}

	AllOverrides.Add(PackId, MoveTemp(Sanitized));
	PackMountSequence.Add(PackId, ++MountSequenceCounter);

	RecomputeWinners();

	UE_LOG(LogDPData, Log, TEXT("[ModContent] Applied %d override(s) from pack '%s'; %d tag(s) now overridden."),
		AllOverrides[PackId].Num(), *PackId.ToString(), WinningByTag.Num());

	BroadcastOverridesChanged();
}

void UMod_ContentRegistrySubsystem::RemovePackOverrides(FGameplayTag PackId)
{
	if (!PackId.IsValid())
	{
		return;
	}

	const int32 Removed = AllOverrides.Remove(PackId);
	PackMountSequence.Remove(PackId);
	if (Removed == 0)
	{
		return; // Idempotent: nothing recorded for this pack.
	}

	RecomputeWinners();

	UE_LOG(LogDPData, Log, TEXT("[ModContent] Removed overrides from pack '%s'; %d tag(s) remain overridden."),
		*PackId.ToString(), WinningByTag.Num());

	BroadcastOverridesChanged();
}

void UMod_ContentRegistrySubsystem::ClearAllOverrides()
{
	const bool bHadAny = AllOverrides.Num() > 0 || WinningByTag.Num() > 0;
	AllOverrides.Reset();
	PackMountSequence.Reset();
	WinningByTag.Reset();
	if (bHadAny)
	{
		BroadcastOverridesChanged();
	}
}

void UMod_ContentRegistrySubsystem::RecomputeWinners()
{
	WinningByTag.Reset();

	// Apply the precedence policy: for each DataTag the winner is the contribution with the highest
	// LoadPriority; ties are broken by the LATER mount sequence (most recently mounted pack wins).
	for (const TPair<FGameplayTag, TArray<FMod_AssetOverride>>& PackPair : AllOverrides)
	{
		const uint64 ThisPackSeq = PackMountSequence.FindRef(PackPair.Key);

		for (const FMod_AssetOverride& Candidate : PackPair.Value)
		{
			if (!Candidate.DataTag.IsValid())
			{
				continue;
			}

			const FMod_AssetOverride* Current = WinningByTag.Find(Candidate.DataTag);
			if (Current == nullptr)
			{
				WinningByTag.Add(Candidate.DataTag, Candidate);
				continue;
			}

			const bool bHigherPriority = Candidate.LoadPriority > Current->LoadPriority;
			const bool bEqualPriorityLaterMount =
				Candidate.LoadPriority == Current->LoadPriority &&
				ThisPackSeq > PackMountSequence.FindRef(Current->SourcePackId);

			if (bHigherPriority || bEqualPriorityLaterMount)
			{
				WinningByTag[Candidate.DataTag] = Candidate;
			}
		}
	}
}

void UMod_ContentRegistrySubsystem::BroadcastOverridesChanged() const
{
	// Best-effort: the message bus is an inert no-op if absent (module independently removable).
	if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// Empty payload: the channel itself carries the "overrides changed, re-resolve" semantics.
		Bus->BroadcastPayload(ModRegistryTags::Bus_OverridesChanged, FInstancedStruct(), const_cast<UMod_ContentRegistrySubsystem*>(this));
	}
}

bool UMod_ContentRegistrySubsystem::RegisterOverrideProvider()
{
	// The shipped UDP_DataRegistrySubsystem (read on disk) exposes no override-provider extension point.
	// Rather than hard-call an API that may not exist (which would not compile), we treat the hook as
	// OPTIONAL and resolve it reflectively: if a future core build adds e.g.
	// "RegisterOverrideProvider(TScriptInterface<...>)" we can be wired in by that build's own glue.
	//
	// Because no such reflected entry point exists today, this stays in FRONT-DOOR mode: THIS subsystem
	// is the documented resolution front door and the core extension is REQUIRED for transparent
	// FindByTag interception. We surface that requirement clearly and return false.
	if (UDP_DataRegistrySubsystem* Base = ResolveBaseRegistry())
	{
		static const FName HookName(TEXT("RegisterModOverrideProvider"));
		if (const UFunction* Hook = Base->FindFunction(HookName))
		{
			// A core build that adds the hook owns the wiring contract; record that it is present so the
			// debug string reflects reality. We do not blind-call an unknown signature.
			(void)Hook;
			bWiredIntoCoreHook = true;
			UE_LOG(LogDPData, Log,
				TEXT("[ModContent] Core data registry exposes '%s'; front-door remains authoritative for resolution."),
				*HookName.ToString());
			return true;
		}
	}

	bWiredIntoCoreHook = false;
	UE_LOG(LogDPData, Log,
		TEXT("[ModContent] Core data registry has no override-provider hook. Resolve mod-aware content via ")
		TEXT("UMod_ContentRegistrySubsystem (ResolveAsset/FindOverriddenAsset); direct UDP_DataRegistrySubsystem::FindByTag will NOT honour overrides."));
	return false;
}

void UMod_ContentRegistrySubsystem::UnregisterOverrideProvider()
{
	bWiredIntoCoreHook = false;
}

FString UMod_ContentRegistrySubsystem::GetDPDebugString_Implementation() const
{
	int32 Contributions = 0;
	for (const TPair<FGameplayTag, TArray<FMod_AssetOverride>>& Pair : AllOverrides)
	{
		Contributions += Pair.Value.Num();
	}
	return FString::Printf(
		TEXT("ModContentRegistry: packs=%d contributions=%d winners=%d coreHook=%s"),
		AllOverrides.Num(), Contributions, WinningByTag.Num(),
		bWiredIntoCoreHook ? TEXT("yes") : TEXT("front-door"));
}
