// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Manager/Mod_ContentManagerSubsystem.h"
#include "DesignPatternsModContentModule.h"
#include "Settings/Mod_DeveloperSettings.h"
#include "Descriptor/Mod_ContentPackDescriptor.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Interfaces/IPluginManager.h"
#include "IPlatformFilePak.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// Defensive fallback used ONLY when the settings CDO is somehow null (documented at call sites): the
// project "Mods" directory is the conservative sandbox so discovery is never unbounded.
namespace
{
	FString GetDefaultSandboxRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Mods"));
	}
}

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_ContentManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the locator + bus to exist before we register/listen/broadcast.
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	Collection.InitializeDependency(UDP_MessageBusSubsystem::StaticClass());

	// Detect whether runtime pak mounting is meaningful. On editor / cooked-iterative there may be no
	// PakPlatformFile in the chain; plugin packs still mount, but raw .pak mounting becomes a guarded
	// no-op (rejected with a clear reason) rather than silently failing.
	bPakMountingAvailable = (FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()) != nullptr)
		|| FPlatformFileManager::Get().GetPlatformFile().GetLowerLevel() != nullptr;

	// Register ourselves as the content service so other systems resolve the manager by tag.
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(ModTags::Service_ModContent, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}

	// Always discover at start (cheap, side-effect-free). Mounting is gated by policy.
	const int32 NumEligible = DiscoverPacks();

	const UMod_DeveloperSettings* Settings = GetSettings();
	const EMod_AutoMountPolicy AutoPolicy = Settings ? Settings->AutoMountPolicy : EMod_AutoMountPolicy::Off;
	if (AutoPolicy == EMod_AutoMountPolicy::OnStartup)
	{
		const int32 Mounted = MountAllDiscovered();
		UE_LOG(LogDP, Log, TEXT("ModContent: auto-mounted %d/%d eligible pack(s) at startup."), Mounted, NumEligible);
	}
	else
	{
		UE_LOG(LogDP, Log, TEXT("ModContent: discovered %d eligible pack(s); auto-mount OFF (awaiting explicit MountPack)."), NumEligible);
	}
}

void UMod_ContentManagerSubsystem::Deinitialize()
{
	// Unmount everything so a torn-down GameInstance does not leave paks layered / plugins mounted.
	UnmountAll();

	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->UnregisterService(ModTags::Service_ModContent);
	}

	WeakSources.Reset();
	Packs.Reset();

	Super::Deinitialize();
}

// =====================================================================================================
// Source registration
// =====================================================================================================

void UMod_ContentManagerSubsystem::RegisterContentSource(const TScriptInterface<IMod_ContentSource>& Source)
{
	if (!Source.GetObject() || !Source.GetInterface())
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: RegisterContentSource ignored a null/invalid source."));
		return;
	}

	PruneSources();

	// De-dup by underlying object.
	for (const TWeakInterfacePtr<IMod_ContentSource>& Existing : WeakSources)
	{
		if (Existing.GetObject() == Source.GetObject())
		{
			return;
		}
	}

	// Construct the weak interface ref from the interface pointer (non-owning; pruned on use).
	const TWeakInterfacePtr<IMod_ContentSource> Weak(*Source.GetInterface());
	WeakSources.Add(Weak);
}

void UMod_ContentManagerSubsystem::UnregisterContentSource(const TScriptInterface<IMod_ContentSource>& Source)
{
	const UObject* Target = Source.GetObject();
	WeakSources.RemoveAll([Target](const TWeakInterfacePtr<IMod_ContentSource>& Weak)
	{
		return !Weak.IsValid() || Weak.GetObject() == Target;
	});
}

void UMod_ContentManagerSubsystem::PruneSources()
{
	WeakSources.RemoveAll([](const TWeakInterfacePtr<IMod_ContentSource>& Weak)
	{
		return !Weak.IsValid();
	});
}

void UMod_ContentManagerSubsystem::SetResolutionPolicy(const TScriptInterface<ISeam_ModResolutionPolicy>& InPolicy)
{
	if (UObject* Obj = InPolicy.GetObject())
	{
		if (Obj->GetClass()->ImplementsInterface(USeam_ModResolutionPolicy::StaticClass()))
		{
			WeakResolutionPolicy = TWeakInterfacePtr<ISeam_ModResolutionPolicy>(InPolicy);
			return;
		}
	}
	UE_LOG(LogDP, Warning, TEXT("ModContent: SetResolutionPolicy ignored a null/non-implementing policy."));
}

void UMod_ContentManagerSubsystem::ClearResolutionPolicy()
{
	WeakResolutionPolicy.Reset();
}

TScriptInterface<ISeam_ModResolutionPolicy> UMod_ContentManagerSubsystem::ResolveResolutionPolicy() const
{
	TScriptInterface<ISeam_ModResolutionPolicy> Result;

	// Explicit weak ref first (pruned on use): if it expired, drop it.
	if (WeakResolutionPolicy.IsValid())
	{
		UObject* Obj = WeakResolutionPolicy.GetObject();
		Result.SetObject(Obj);
		Result.SetInterface(Cast<ISeam_ModResolutionPolicy>(Obj));
		return Result;
	}

	// Fall back to the service locator (DP.Service.Mod.Resolution).
	if (const UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (UObject* Provider = Locator->ResolveService(ModTags::Service_ModResolution))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_ModResolutionPolicy::StaticClass()))
			{
				Result.SetObject(Provider);
				Result.SetInterface(Cast<ISeam_ModResolutionPolicy>(Provider));
			}
		}
	}
	return Result;
}

// =====================================================================================================
// Discovery
// =====================================================================================================

int32 UMod_ContentManagerSubsystem::DiscoverPacks()
{
	const UMod_DeveloperSettings* Settings = GetSettings();

	// Merge raw discovery from every source. Plugin manager + directories first so a content-source
	// override of the same id (later) takes precedence (the merge keeps the last seen disk info but
	// preserves the first-seen provenance for logging).
	TArray<FMod_PackInfo> Raw;
	GatherFromPluginManager(Raw);
	GatherFromDirectories(Raw);
	GatherFromSources(Raw);

	// Preserve the previously-Mounted state for packs that are still present, so re-discovery does not
	// silently drop mount handles. We rebuild the map but copy forward mount info for survivors.
	TMap<FGameplayTag, FMod_MountedPack> Previous = MoveTemp(Packs);
	Packs.Reset();

	int32 NumEligible = 0;
	int32 DiscoveryOrdinal = 0;

	for (FMod_PackInfo& Info : Raw)
	{
		if (!Info.IsUsable())
		{
			UE_LOG(LogDP, Verbose, TEXT("ModContent: skipping unusable discovered pack (invalid id/path)."));
			continue;
		}

		// Skip a second sighting of the same id: first usable record wins (stable provenance).
		if (Packs.Contains(Info.PackId))
		{
			UE_LOG(LogDP, Verbose, TEXT("ModContent: duplicate pack id '%s' from source '%s' ignored."),
				*Info.PackId.GetTagName().ToString(), *Info.SourceId.GetTagName().ToString());
			continue;
		}

		FMod_MountedPack Record;
		Record.Info = Info;
		Record.OrderIndex = DiscoveryOrdinal++;

		// Allowlist / denylist eligibility. Ineligible packs are recorded as Rejected (so tooling can
		// show WHY) but never counted as eligible and never mounted.
		const bool bEligible = Settings ? Settings->IsPackIdEligible(Info.PackId)
		                                : false; // defensive: no settings => fail closed (nothing eligible)
		if (!bEligible)
		{
			Record.State = EMod_PackState::Rejected;
			FMod_ValidationMessage Msg;
			Msg.Severity = EMod_ValidationResult::Fail;
			Msg.Reason = ModTags::Mod;
			Msg.Detail = NSLOCTEXT("ModContent", "NotAllowlisted", "Pack id is not permitted by the allowlist/denylist policy.");
			Record.LastValidation.Messages.Add(Msg);
			Record.LastValidation.RecomputeResult();
		}
		else
		{
			Record.State = EMod_PackState::Discovered;
			++NumEligible;
		}

		// Carry forward a live mount from the previous map (same id still present & previously mounted).
		if (const FMod_MountedPack* Prev = Previous.Find(Info.PackId))
		{
			if (Prev->State == EMod_PackState::Mounted && bEligible)
			{
				Record.State = EMod_PackState::Mounted;
				Record.ActiveMountPoint = Prev->ActiveMountPoint;
				Record.LastValidation = Prev->LastValidation;
			}
		}

		Packs.Add(Info.PackId, MoveTemp(Record));
	}

	// Any previously-mounted pack that vanished from discovery must be unmounted to avoid a leak.
	for (TPair<FGameplayTag, FMod_MountedPack>& Pair : Previous)
	{
		if (Pair.Value.State == EMod_PackState::Mounted && !Packs.Contains(Pair.Key))
		{
			UE_LOG(LogDP, Warning, TEXT("ModContent: pack '%s' disappeared from discovery while mounted; unmounting."),
				*Pair.Key.GetTagName().ToString());
			DoEngineUnmount(Pair.Value);
		}
	}

	ResolveMountOrder();

	return NumEligible;
}

void UMod_ContentManagerSubsystem::GatherFromSources(TArray<FMod_PackInfo>& OutRaw) const
{
	// Explicitly-registered weak sources first.
	for (const TWeakInterfacePtr<IMod_ContentSource>& Weak : WeakSources)
	{
		if (!Weak.IsValid())
		{
			continue;
		}
		IMod_ContentSource* Src = Weak.Get();
		if (Src)
		{
			TArray<FMod_PackInfo> Local;
			IMod_ContentSource::Execute_EnumeratePacks(Weak.GetObject(), Local);
			OutRaw.Append(MoveTemp(Local));
		}
	}

	// Service-locator-registered source (resolved fresh; never stored — avoids holding a world object).
	if (const UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (UObject* Provider = Locator->ResolveService(ModTags::Service_Source))
		{
			if (Provider->GetClass()->ImplementsInterface(UMod_ContentSource::StaticClass()))
			{
				TArray<FMod_PackInfo> Local;
				IMod_ContentSource::Execute_EnumeratePacks(Provider, Local);
				OutRaw.Append(MoveTemp(Local));
			}
		}
	}
}

void UMod_ContentManagerSubsystem::GatherFromPluginManager(TArray<FMod_PackInfo>& OutRaw) const
{
	IPluginManager& PluginMgr = IPluginManager::Get();

	// Content-only, explicitly-loadable plugins are the safe "mod plugin" shape. We surface every
	// discovered (not necessarily enabled) plugin that is content-only and not already mounted by the
	// engine, leaving the actual mount decision to validate-before-activate. Engine/built-in plugins
	// are excluded by requiring the plugin live under the project plugins dir (sandbox).
	const FString ProjectPluginsDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());

	for (const TSharedRef<IPlugin>& Plugin : PluginMgr.GetDiscoveredPlugins())
	{
		const FPluginDescriptor& Desc = Plugin->GetDescriptor();

		// Only consider content-only plugins (no modules => no executable code to ever run).
		if (Desc.Modules.Num() > 0)
		{
			continue;
		}

		const FString PluginBaseDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		if (!PluginBaseDir.StartsWith(ProjectPluginsDir))
		{
			// Outside the project plugins sandbox; not a mod candidate.
			continue;
		}

		// Pack id is derived from the plugin name under DP.Mod.Pack so a content plugin needs no extra
		// descriptor to be addressable. A descriptor asset inside the pack can refine deps/versions.
		const FString TagString = FString::Printf(TEXT("DP.Mod.Pack.%s"), *Plugin->GetName());
		const FGameplayTag PackId = FGameplayTag::RequestGameplayTag(FName(*TagString), /*ErrorIfNotFound*/ false);
		if (!PackId.IsValid())
		{
			// The project has not registered a tag for this plugin; skip rather than fabricate one.
			UE_LOG(LogDP, Verbose, TEXT("ModContent: content plugin '%s' has no DP.Mod.Pack.* tag; skipping."), *Plugin->GetName());
			continue;
		}

		FMod_PackInfo Info;
		Info.PackId = PackId;
		Info.Kind = EMod_PackKind::Plugin;
		Info.PluginName = Plugin->GetName();
		Info.DiskPath = Plugin->GetDescriptorFileName();
		Info.SourceId = ModTags::Source;
		OutRaw.Add(MoveTemp(Info));
	}
}

void UMod_ContentManagerSubsystem::GatherFromDirectories(TArray<FMod_PackInfo>& OutRaw) const
{
	const UMod_DeveloperSettings* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}

	IFileManager& FileMgr = IFileManager::Get();

	for (const FDirectoryPath& Dir : Settings->DiscoveryDirectories)
	{
		const FString AbsDir = FPaths::ConvertRelativePathToFull(
			FPaths::IsRelative(Dir.Path) ? (FPaths::ProjectDir() / Dir.Path) : Dir.Path);

		if (!FileMgr.DirectoryExists(*AbsDir))
		{
			UE_LOG(LogDP, Verbose, TEXT("ModContent: discovery directory '%s' does not exist."), *AbsDir);
			continue;
		}

		// Find .pak files directly under the directory tree. Each pak's id is derived from its file
		// stem under DP.Mod.Pack; a sibling descriptor refines metadata (handled by the validator).
		TArray<FString> PakFiles;
		FileMgr.FindFilesRecursive(PakFiles, *AbsDir, TEXT("*.pak"), /*Files*/ true, /*Dirs*/ false);

		for (const FString& PakFile : PakFiles)
		{
			const FString Stem = FPaths::GetBaseFilename(PakFile);
			const FString TagString = FString::Printf(TEXT("DP.Mod.Pack.%s"), *Stem);
			const FGameplayTag PackId = FGameplayTag::RequestGameplayTag(FName(*TagString), /*ErrorIfNotFound*/ false);
			if (!PackId.IsValid())
			{
				UE_LOG(LogDP, Verbose, TEXT("ModContent: pak '%s' has no DP.Mod.Pack.* tag; skipping."), *PakFile);
				continue;
			}

			FMod_PackInfo Info;
			Info.PackId = PackId;
			Info.Kind = EMod_PackKind::Pak;
			Info.DiskPath = FPaths::ConvertRelativePathToFull(PakFile);
			// Mount point is sandboxed under the pak's containing folder, exposed as an engine virtual
			// mount; the actual virtual path is the pak stem so content addresses as /<Stem>/...
			Info.MountPoint = FString::Printf(TEXT("../../../%s/Mods/%s/"), FApp::GetProjectName(), *Stem);
			Info.SourceId = ModTags::Source;
			OutRaw.Add(MoveTemp(Info));
		}
	}
}

// =====================================================================================================
// Order resolution (topological sort)
// =====================================================================================================

void UMod_ContentManagerSubsystem::ResolveMountOrder()
{
	const UMod_DeveloperSettings* Settings = GetSettings();
	const EMod_MountOrderPolicy TiePolicy = Settings ? Settings->MountOrderPolicy : EMod_MountOrderPolicy::DiscoveryOrder;

	// Build the set of eligible (non-Rejected) ids participating in ordering.
	TArray<FGameplayTag> Eligible;
	Eligible.Reserve(Packs.Num());
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		if (Pair.Value.State != EMod_PackState::Rejected)
		{
			Eligible.Add(Pair.Key);
		}
	}

	// ADDITIVE: when an optional resolution policy is installed, pre-score every eligible pack so its
	// score can act as the HIGHEST-precedence tie-break among independent packs (higher score sorts
	// EARLIER here, mirroring the existing "lower index mounts first" convention). With no policy the
	// score map is empty and FindRef yields 0 for all, so the shipped ordering is bit-for-bit unchanged.
	TMap<FGameplayTag, int32> PolicyScores;
	if (TScriptInterface<ISeam_ModResolutionPolicy> Policy = ResolveResolutionPolicy())
	{
		for (const FGameplayTag& Id : Eligible)
		{
			PolicyScores.Add(Id, ISeam_ModResolutionPolicy::Execute_ScoreLoadOrder(Policy.GetObject(), Id));
		}
	}

	// Stable tie-break ordering of the eligible set before the sort, so the topo result is deterministic.
	Eligible.Sort([this, TiePolicy, Settings, &PolicyScores](const FGameplayTag& A, const FGameplayTag& B)
	{
		if (PolicyScores.Num() > 0)
		{
			const int32 Sa = PolicyScores.FindRef(A);
			const int32 Sb = PolicyScores.FindRef(B);
			if (Sa != Sb) { return Sa > Sb; } // higher-scored pack mounts earlier
		}
		if (TiePolicy == EMod_MountOrderPolicy::Alphabetical)
		{
			return A.GetTagName().LexicalLess(B.GetTagName());
		}
		if (TiePolicy == EMod_MountOrderPolicy::Explicit && Settings)
		{
			const int32 Ia = Settings->ExplicitMountOrder.IndexOfByKey(A);
			const int32 Ib = Settings->ExplicitMountOrder.IndexOfByKey(B);
			const int32 Ra = (Ia == INDEX_NONE) ? MAX_int32 : Ia;
			const int32 Rb = (Ib == INDEX_NONE) ? MAX_int32 : Ib;
			if (Ra != Rb) { return Ra < Rb; }
		}
		// DiscoveryOrder (and ties under other policies): by the discovery ordinal captured at discovery.
		const FMod_MountedPack* Pa = Packs.Find(A);
		const FMod_MountedPack* Pb = Packs.Find(B);
		const int32 Oa = Pa ? Pa->OrderIndex : MAX_int32;
		const int32 Ob = Pb ? Pb->OrderIndex : MAX_int32;
		return Oa < Ob;
	});

	// Kahn's algorithm over hard dependencies that are themselves eligible & present.
	TSet<FGameplayTag> EligibleSet(Eligible);
	TMap<FGameplayTag, int32> InDegree;
	TMap<FGameplayTag, TArray<FGameplayTag>> Dependents; // dep -> packs that depend on it

	auto GetDescriptor = [](const FMod_MountedPack& Rec) -> const UMod_ContentPackDescriptor*
	{
		return Rec.Info.Descriptor.Get();
	};

	for (const FGameplayTag& Id : Eligible)
	{
		InDegree.FindOrAdd(Id, 0);
	}

	for (const FGameplayTag& Id : Eligible)
	{
		const FMod_MountedPack& Rec = Packs.FindChecked(Id);
		if (const UMod_ContentPackDescriptor* Desc = GetDescriptor(Rec))
		{
			for (const FMod_PackDependency& Dep : Desc->Dependencies)
			{
				if (!Dep.DependencyId.IsValid())
				{
					continue;
				}
				const bool bDepPresent = EligibleSet.Contains(Dep.DependencyId);
				if (!bDepPresent)
				{
					// Hard-missing dependency: reject this pack now (cannot be ordered/mounted).
					if (!Dep.bOptional)
					{
						FMod_MountedPack& Mutable = Packs.FindChecked(Id);
						Mutable.State = EMod_PackState::Rejected;
						FMod_ValidationMessage Msg;
						Msg.Severity = EMod_ValidationResult::Fail;
						Msg.Reason = ModTags::Mod;
						Msg.Detail = FText::Format(
							NSLOCTEXT("ModContent", "MissingDep", "Required dependency '{0}' is missing."),
							FText::FromName(Dep.DependencyId.GetTagName()));
						Mutable.LastValidation.Messages.Add(Msg);
						Mutable.LastValidation.RecomputeResult();
					}
					continue;
				}
				// Edge Dep -> Id : Id depends on Dep, so Dep must mount first.
				InDegree.FindOrAdd(Id)++;
				Dependents.FindOrAdd(Dep.DependencyId).Add(Id);
			}
		}
	}

	// Remove freshly-rejected packs from the working set before running Kahn.
	Eligible.RemoveAll([this](const FGameplayTag& Id)
	{
		const FMod_MountedPack* Rec = Packs.Find(Id);
		return !Rec || Rec->State == EMod_PackState::Rejected;
	});

	// Seed the queue with all zero-in-degree nodes, preserving the tie-break order.
	TArray<FGameplayTag> Queue;
	for (const FGameplayTag& Id : Eligible)
	{
		if (InDegree.FindRef(Id) == 0)
		{
			Queue.Add(Id);
		}
	}

	TArray<FGameplayTag> Sorted;
	Sorted.Reserve(Eligible.Num());
	int32 Head = 0;
	while (Head < Queue.Num())
	{
		const FGameplayTag Id = Queue[Head++];
		Sorted.Add(Id);
		for (const FGameplayTag& Dependent : Dependents.FindOrAdd(Id))
		{
			int32& Deg = InDegree.FindChecked(Dependent);
			if (--Deg == 0)
			{
				Queue.Add(Dependent);
			}
		}
	}

	// Anything not emitted is part of a cycle: reject every such pack.
	if (Sorted.Num() != Eligible.Num())
	{
		TSet<FGameplayTag> SortedSet(Sorted);
		for (const FGameplayTag& Id : Eligible)
		{
			if (!SortedSet.Contains(Id))
			{
				FMod_MountedPack& Rec = Packs.FindChecked(Id);
				Rec.State = EMod_PackState::Rejected;
				FMod_ValidationMessage Msg;
				Msg.Severity = EMod_ValidationResult::Fail;
				Msg.Reason = ModTags::Mod;
				Msg.Detail = NSLOCTEXT("ModContent", "DepCycle", "Pack is part of a dependency cycle and cannot be ordered.");
				Rec.LastValidation.Messages.Add(Msg);
				Rec.LastValidation.RecomputeResult();
				UE_LOG(LogDP, Error, TEXT("ModContent: pack '%s' rejected (dependency cycle)."), *Id.GetTagName().ToString());
			}
		}
	}

	// Write back the resolved order indices.
	for (int32 i = 0; i < Sorted.Num(); ++i)
	{
		Packs.FindChecked(Sorted[i]).OrderIndex = i;
	}
}

// =====================================================================================================
// Mount / unmount
// =====================================================================================================

bool UMod_ContentManagerSubsystem::MountPack(FGameplayTag PackId)
{
	FMod_MountedPack* Record = Packs.Find(PackId);
	if (!Record)
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: MountPack('%s') — unknown pack (run DiscoverPacks first)."), *PackId.GetTagName().ToString());
		return false;
	}
	if (Record->State == EMod_PackState::Mounted)
	{
		return true; // idempotent
	}
	if (Record->State == EMod_PackState::Rejected)
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: MountPack('%s') refused — pack was rejected at discovery."), *PackId.GetTagName().ToString());
		return false;
	}

	TSet<FGameplayTag> InProgress;
	return MountRecordInternal(*Record, /*bAllowDependencyMount*/ true, InProgress);
}

int32 UMod_ContentManagerSubsystem::MountAllDiscovered()
{
	// Mount in ascending resolved order so dependencies precede dependents.
	TArray<FGameplayTag> Order;
	Order.Reserve(Packs.Num());
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		if (Pair.Value.State == EMod_PackState::Discovered)
		{
			Order.Add(Pair.Key);
		}
	}
	Order.Sort([this](const FGameplayTag& A, const FGameplayTag& B)
	{
		return Packs.FindChecked(A).OrderIndex < Packs.FindChecked(B).OrderIndex;
	});

	int32 Count = 0;
	for (const FGameplayTag& Id : Order)
	{
		FMod_MountedPack* Record = Packs.Find(Id);
		if (Record && Record->State == EMod_PackState::Discovered)
		{
			TSet<FGameplayTag> InProgress;
			if (MountRecordInternal(*Record, /*bAllowDependencyMount*/ true, InProgress))
			{
				++Count;
			}
		}
	}
	return Count;
}

bool UMod_ContentManagerSubsystem::MountRecordInternal(FMod_MountedPack& Record, bool bAllowDependencyMount, TSet<FGameplayTag>& InProgress)
{
	const FGameplayTag PackId = Record.Info.PackId;

	if (Record.State == EMod_PackState::Mounted)
	{
		return true;
	}
	if (InProgress.Contains(PackId))
	{
		// Re-entrancy through a cycle (should have been caught by ordering; defensive).
		UE_LOG(LogDP, Error, TEXT("ModContent: dependency cycle hit while mounting '%s'."), *PackId.GetTagName().ToString());
		return false;
	}
	InProgress.Add(PackId);

	// --- Hard guard 1: sandbox-root policy ---
	if (!PassesSandboxPolicy(Record.Info))
	{
		Record.State = EMod_PackState::Rejected;
		FMod_ValidationMessage Msg;
		Msg.Severity = EMod_ValidationResult::Fail;
		Msg.Reason = ModTags::Mod;
		Msg.Detail = NSLOCTEXT("ModContent", "SandboxViolation", "Pack content path escapes the configured sandbox roots.");
		Record.LastValidation.Messages.Add(Msg);
		Record.LastValidation.RecomputeResult();
		UE_LOG(LogDP, Error, TEXT("ModContent: pack '%s' rejected (sandbox violation)."), *PackId.GetTagName().ToString());
		BroadcastMountEvent(ModTags::Bus_Rejected, Record, EMod_ValidationResult::Fail);
		return false;
	}

	// --- Hard guard 2: version compatibility ---
	FMod_ValidationReport Report;
	if (!PassesVersionGates(Record.Info, Report))
	{
		Record.State = EMod_PackState::Rejected;
		Record.LastValidation = Report;
		UE_LOG(LogDP, Error, TEXT("ModContent: pack '%s' rejected (version gate)."), *PackId.GetTagName().ToString());
		BroadcastMountEvent(ModTags::Bus_Rejected, Record, EMod_ValidationResult::Fail);
		return false;
	}

	// --- Mount hard dependencies first ---
	if (const UMod_ContentPackDescriptor* Desc = Record.Info.Descriptor.Get())
	{
		for (const FMod_PackDependency& Dep : Desc->Dependencies)
		{
			if (!Dep.DependencyId.IsValid())
			{
				continue;
			}
			FMod_MountedPack* DepRecord = Packs.Find(Dep.DependencyId);
			const bool bDepUsable = DepRecord && DepRecord->State != EMod_PackState::Rejected;
			if (!bDepUsable)
			{
				if (Dep.bOptional)
				{
					continue; // optional missing dep — proceed
				}
				Record.State = EMod_PackState::Rejected;
				FMod_ValidationMessage Msg;
				Msg.Severity = EMod_ValidationResult::Fail;
				Msg.Reason = ModTags::Mod;
				Msg.Detail = FText::Format(
					NSLOCTEXT("ModContent", "DepUnavailable", "Required dependency '{0}' is unavailable."),
					FText::FromName(Dep.DependencyId.GetTagName()));
				Report.Messages.Add(Msg);
				Report.RecomputeResult();
				Record.LastValidation = Report;
				BroadcastMountEvent(ModTags::Bus_Rejected, Record, EMod_ValidationResult::Fail);
				return false;
			}
			if (DepRecord->State != EMod_PackState::Mounted)
			{
				if (!bAllowDependencyMount || !MountRecordInternal(*DepRecord, bAllowDependencyMount, InProgress))
				{
					Record.State = EMod_PackState::Rejected;
					FMod_ValidationMessage Msg;
					Msg.Severity = EMod_ValidationResult::Fail;
					Msg.Reason = ModTags::Mod;
					Msg.Detail = FText::Format(
						NSLOCTEXT("ModContent", "DepMountFailed", "Required dependency '{0}' failed to mount."),
						FText::FromName(Dep.DependencyId.GetTagName()));
					Report.Messages.Add(Msg);
					Report.RecomputeResult();
					Record.LastValidation = Report;
					BroadcastMountEvent(ModTags::Bus_Rejected, Record, EMod_ValidationResult::Fail);
					return false;
				}
			}
		}
	}

	// --- VALIDATE-BEFORE-ACTIVATE: run the validator seam ---
	{
		TArray<FMod_PackInfo> Known;
		Known.Reserve(Packs.Num());
		for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
		{
			Known.Add(Pair.Value.Info);
		}

		TScriptInterface<IMod_PackValidator> Validator = ResolveValidator();
		if (Validator.GetObject() && Validator.GetInterface())
		{
			const FMod_ValidationReport ValReport =
				IMod_PackValidator::Execute_ValidatePack(Validator.GetObject(), Record.Info, Known);
			Report.Messages.Append(ValReport.Messages);
			Report.RecomputeResult();
		}
		else
		{
			// Inert default: no validator registered. The manager's hard guards above still applied;
			// the validator's portion is treated as Pass (documented inert behaviour).
			UE_LOG(LogDP, Verbose, TEXT("ModContent: no validator registered; mounting '%s' on hard guards only."), *PackId.GetTagName().ToString());
		}
	}

	Record.LastValidation = Report;

	if (!Report.AllowsMount())
	{
		Record.State = EMod_PackState::Rejected;
		UE_LOG(LogDP, Warning, TEXT("ModContent: pack '%s' rejected by validator."), *PackId.GetTagName().ToString());
		BroadcastMountEvent(ModTags::Bus_Rejected, Record, Report.Result);
		return false;
	}

	// --- Perform the engine mount (content only; never code) ---
	if (!DoEngineMount(Record))
	{
		Record.State = EMod_PackState::Rejected;
		FMod_ValidationMessage Msg;
		Msg.Severity = EMod_ValidationResult::Fail;
		Msg.Reason = ModTags::Mod;
		Msg.Detail = NSLOCTEXT("ModContent", "EngineMountFailed", "The engine mount operation failed.");
		Report.Messages.Add(Msg);
		Report.RecomputeResult();
		Record.LastValidation = Report;
		BroadcastMountEvent(ModTags::Bus_Rejected, Record, EMod_ValidationResult::Fail);
		return false;
	}

	Record.State = EMod_PackState::Mounted;
	UE_LOG(LogDP, Log, TEXT("ModContent: mounted pack '%s' (%s)%s."),
		*PackId.GetTagName().ToString(),
		Record.Info.Kind == EMod_PackKind::Plugin ? TEXT("plugin") : TEXT("pak"),
		Report.Result == EMod_ValidationResult::Warn ? TEXT(" [with warnings]") : TEXT(""));

	BroadcastMountEvent(ModTags::Bus_Mounted, Record, Report.Result);
	return true;
}

bool UMod_ContentManagerSubsystem::DoEngineMount(FMod_MountedPack& Record)
{
	switch (Record.Info.Kind)
	{
	case EMod_PackKind::Plugin:
	{
		// Mount the content plugin. This registers the plugin's content root; it does NOT load or
		// execute any module (content-only plugins have none — enforced at discovery).
		const bool bOk = IPluginManager::Get().MountExplicitlyLoadedPlugin(Record.Info.PluginName);
		if (!bOk)
		{
			UE_LOG(LogDP, Error, TEXT("ModContent: failed to mount content plugin '%s'."), *Record.Info.PluginName);
		}
		return bOk;
	}
	case EMod_PackKind::Pak:
	{
		if (!bPakMountingAvailable)
		{
			UE_LOG(LogDP, Error, TEXT("ModContent: pak mounting unavailable in this build; cannot mount '%s'."),
				*Record.Info.PackId.GetTagName().ToString());
			return false;
		}

		FPakPlatformFile* PakPlatform = static_cast<FPakPlatformFile*>(
			FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
		if (!PakPlatform)
		{
			// Not in the active platform-file chain: install a transient one over the current lower level.
			UE_LOG(LogDP, Error, TEXT("ModContent: no PakPlatformFile in the active chain; cannot mount '%s'."),
				*Record.Info.PackId.GetTagName().ToString());
			return false;
		}

		const UMod_DeveloperSettings* Settings = GetSettings();
		// Defensive fallback for the base priority if the CDO is null: a sensible positive layering base.
		const int32 BasePriority = Settings ? Settings->BasePakMountPriority : 1000;

		const FString MountPoint = Record.Info.MountPoint;
		const bool bOk = PakPlatform->Mount(*Record.Info.DiskPath, BasePriority, MountPoint.IsEmpty() ? nullptr : *MountPoint);
		if (bOk)
		{
			Record.ActiveMountPoint = MountPoint;
		}
		else
		{
			UE_LOG(LogDP, Error, TEXT("ModContent: IPlatformFilePak::Mount failed for '%s'."), *Record.Info.DiskPath);
		}
		return bOk;
	}
	default:
		return false;
	}
}

bool UMod_ContentManagerSubsystem::UnmountPack(FGameplayTag PackId)
{
	FMod_MountedPack* Record = Packs.Find(PackId);
	if (!Record || Record->State != EMod_PackState::Mounted)
	{
		return false;
	}

	// Refuse to unmount a pack that a currently-mounted pack hard-depends on.
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		if (Pair.Value.State != EMod_PackState::Mounted || Pair.Key == PackId)
		{
			continue;
		}
		if (const UMod_ContentPackDescriptor* Desc = Pair.Value.Info.Descriptor.Get())
		{
			for (const FMod_PackDependency& Dep : Desc->Dependencies)
			{
				if (!Dep.bOptional && Dep.DependencyId == PackId)
				{
					UE_LOG(LogDP, Warning, TEXT("ModContent: cannot unmount '%s' — '%s' depends on it."),
						*PackId.GetTagName().ToString(), *Pair.Key.GetTagName().ToString());
					return false;
				}
			}
		}
	}

	if (!DoEngineUnmount(*Record))
	{
		return false;
	}

	Record->State = EMod_PackState::Discovered;
	Record->ActiveMountPoint.Reset();
	UE_LOG(LogDP, Log, TEXT("ModContent: unmounted pack '%s'."), *PackId.GetTagName().ToString());
	BroadcastMountEvent(ModTags::Bus_Unmounted, *Record, EMod_ValidationResult::Pass);
	return true;
}

int32 UMod_ContentManagerSubsystem::UnmountAll()
{
	// Reverse resolved order so dependents come down before their dependencies.
	TArray<FGameplayTag> Order;
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		if (Pair.Value.State == EMod_PackState::Mounted)
		{
			Order.Add(Pair.Key);
		}
	}
	Order.Sort([this](const FGameplayTag& A, const FGameplayTag& B)
	{
		return Packs.FindChecked(A).OrderIndex > Packs.FindChecked(B).OrderIndex;
	});

	int32 Count = 0;
	for (const FGameplayTag& Id : Order)
	{
		FMod_MountedPack* Record = Packs.Find(Id);
		if (Record && Record->State == EMod_PackState::Mounted)
		{
			if (DoEngineUnmount(*Record))
			{
				Record->State = EMod_PackState::Discovered;
				Record->ActiveMountPoint.Reset();
				BroadcastMountEvent(ModTags::Bus_Unmounted, *Record, EMod_ValidationResult::Pass);
				++Count;
			}
		}
	}
	return Count;
}

bool UMod_ContentManagerSubsystem::DoEngineUnmount(FMod_MountedPack& Record)
{
	switch (Record.Info.Kind)
	{
	case EMod_PackKind::Plugin:
	{
		// UnmountExplicitlyLoadedPlugin removes the content root previously registered by mounting.
		const bool bOk = IPluginManager::Get().UnmountExplicitlyLoadedPlugin(Record.Info.PluginName, /*OutReason*/ nullptr);
		if (!bOk)
		{
			UE_LOG(LogDP, Error, TEXT("ModContent: failed to unmount content plugin '%s'."), *Record.Info.PluginName);
		}
		return bOk;
	}
	case EMod_PackKind::Pak:
	{
		FPakPlatformFile* PakPlatform = static_cast<FPakPlatformFile*>(
			FPlatformFileManager::Get().FindPlatformFile(FPakPlatformFile::GetTypeName()));
		if (!PakPlatform)
		{
			return false;
		}
		const bool bOk = PakPlatform->Unmount(*Record.Info.DiskPath);
		if (!bOk)
		{
			UE_LOG(LogDP, Error, TEXT("ModContent: IPlatformFilePak::Unmount failed for '%s'."), *Record.Info.DiskPath);
		}
		return bOk;
	}
	default:
		return false;
	}
}

// =====================================================================================================
// Guards
// =====================================================================================================

bool UMod_ContentManagerSubsystem::PassesSandboxPolicy(const FMod_PackInfo& Info) const
{
	const UMod_DeveloperSettings* Settings = GetSettings();

	// Build the effective sandbox-root list. Defensive fallback: if there are no configured roots (or
	// no settings at all) we constrain to the project "Mods" directory — never an unbounded sandbox.
	TArray<FString> Roots;
	if (Settings)
	{
		for (const FDirectoryPath& Dir : Settings->SandboxContentRoots)
		{
			Roots.Add(FPaths::ConvertRelativePathToFull(
				FPaths::IsRelative(Dir.Path) ? (FPaths::ProjectDir() / Dir.Path) : Dir.Path));
		}
	}
	// Plugin packs are also constrained to the project plugins dir by discovery; allow it as a root.
	if (Info.Kind == EMod_PackKind::Plugin)
	{
		Roots.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir()));
	}
	if (Roots.Num() == 0)
	{
		Roots.Add(GetDefaultSandboxRoot());
	}

	const FString DiskFull = FPaths::ConvertRelativePathToFull(Info.DiskPath);

	// The disk path must live under at least one sandbox root (anti path-traversal).
	bool bUnderRoot = false;
	for (const FString& Root : Roots)
	{
		if (DiskFull.StartsWith(Root))
		{
			bUnderRoot = true;
			break;
		}
	}
	if (!bUnderRoot)
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: pack '%s' disk path '%s' is outside all sandbox roots."),
			*Info.PackId.GetTagName().ToString(), *DiskFull);
		return false;
	}

	// A pak's mount point must be relative (a virtual engine path), never an absolute OS path.
	if (Info.Kind == EMod_PackKind::Pak && !Info.MountPoint.IsEmpty() && FPaths::IsDrive(Info.MountPoint))
	{
		return false;
	}

	return true;
}

bool UMod_ContentManagerSubsystem::PassesVersionGates(const FMod_PackInfo& Info, FMod_ValidationReport& InOutReport) const
{
	const UMod_ContentPackDescriptor* Desc = Info.Descriptor.Get();
	if (!Desc)
	{
		// No descriptor => no declared floors => nothing to gate. (A bare content plugin/pak.)
		return true;
	}

	const UMod_DeveloperSettings* Settings = GetSettings();

	// Engine floor: compare against the override, else the real running engine version.
	if (!Desc->MinEngineVersion.IsZero())
	{
		FMod_SemVer HostEngine;
		if (Settings && !Settings->HostEngineVersionOverride.IsZero())
		{
			HostEngine = Settings->HostEngineVersionOverride;
		}
		else
		{
			const FEngineVersion& EV = FEngineVersion::Current();
			HostEngine = FMod_SemVer(EV.GetMajor(), EV.GetMinor(), EV.GetPatch());
		}
		if (!HostEngine.IsAtLeast(Desc->MinEngineVersion))
		{
			FMod_ValidationMessage Msg;
			Msg.Severity = EMod_ValidationResult::Fail;
			Msg.Reason = ModTags::Mod;
			Msg.Detail = FText::Format(
				NSLOCTEXT("ModContent", "EngineTooOld", "Pack requires engine {0}; host is {1}."),
				FText::FromString(Desc->MinEngineVersion.ToString()), FText::FromString(HostEngine.ToString()));
			InOutReport.Messages.Add(Msg);
			InOutReport.RecomputeResult();
			return false;
		}
	}

	// Game floor: compare against the configured host game version (zero disables the gate).
	if (!Desc->MinGameVersion.IsZero())
	{
		const FMod_SemVer HostGame = Settings ? Settings->HostGameVersion : FMod_SemVer();
		if (!HostGame.IsZero() && !HostGame.IsAtLeast(Desc->MinGameVersion))
		{
			FMod_ValidationMessage Msg;
			Msg.Severity = EMod_ValidationResult::Fail;
			Msg.Reason = ModTags::Mod;
			Msg.Detail = FText::Format(
				NSLOCTEXT("ModContent", "GameTooOld", "Pack requires game {0}; host is {1}."),
				FText::FromString(Desc->MinGameVersion.ToString()), FText::FromString(HostGame.ToString()));
			InOutReport.Messages.Add(Msg);
			InOutReport.RecomputeResult();
			return false;
		}
	}

	return true;
}

TScriptInterface<IMod_PackValidator> UMod_ContentManagerSubsystem::ResolveValidator() const
{
	TScriptInterface<IMod_PackValidator> Result;
	if (const UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (UObject* Provider = Locator->ResolveService(ModTags::Service_Validator))
		{
			if (Provider->GetClass()->ImplementsInterface(UMod_PackValidator::StaticClass()))
			{
				Result.SetObject(Provider);
				Result.SetInterface(Cast<IMod_PackValidator>(Provider));
			}
		}
	}
	return Result;
}

// =====================================================================================================
// Queries
// =====================================================================================================

bool UMod_ContentManagerSubsystem::IsPackMounted(FGameplayTag PackId) const
{
	const FMod_MountedPack* Record = Packs.Find(PackId);
	return Record && Record->State == EMod_PackState::Mounted;
}

bool UMod_ContentManagerSubsystem::GetPackRecord(FGameplayTag PackId, FMod_MountedPack& OutRecord) const
{
	if (const FMod_MountedPack* Record = Packs.Find(PackId))
	{
		OutRecord = *Record;
		return true;
	}
	return false;
}

TArray<FMod_MountedPack> UMod_ContentManagerSubsystem::GetMountedPacks() const
{
	TArray<FMod_MountedPack> Out;
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		if (Pair.Value.State == EMod_PackState::Mounted)
		{
			Out.Add(Pair.Value);
		}
	}
	Out.Sort([](const FMod_MountedPack& A, const FMod_MountedPack& B) { return A.OrderIndex < B.OrderIndex; });
	return Out;
}

TArray<FMod_MountedPack> UMod_ContentManagerSubsystem::GetAllPacks() const
{
	TArray<FMod_MountedPack> Out;
	Packs.GenerateValueArray(Out);
	Out.Sort([](const FMod_MountedPack& A, const FMod_MountedPack& B) { return A.OrderIndex < B.OrderIndex; });
	return Out;
}

// =====================================================================================================
// Bus / locator / settings helpers
// =====================================================================================================

void UMod_ContentManagerSubsystem::BroadcastMountEvent(FGameplayTag Channel, const FMod_MountedPack& Record, EMod_ValidationResult ValidationResult) const
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return; // inert when the bus is unavailable
	}

	FMod_PackMountEvent Event;
	Event.PackId = Record.Info.PackId;
	Event.State = Record.State;
	Event.ValidationResult = ValidationResult;

	const FInstancedStruct Payload = FInstancedStruct::Make(Event);
	Bus->BroadcastPayload(Channel, Payload, const_cast<UMod_ContentManagerSubsystem*>(this));
}

UDP_MessageBusSubsystem* UMod_ContentManagerSubsystem::GetBus() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_MessageBusSubsystem>();
	}
	return nullptr;
}

UDP_ServiceLocatorSubsystem* UMod_ContentManagerSubsystem::GetLocator() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	}
	return nullptr;
}

const UMod_DeveloperSettings* UMod_ContentManagerSubsystem::GetSettings() const
{
	const UMod_DeveloperSettings* Settings = UMod_DeveloperSettings::Get();
	// GetDefault is non-null for a registered UDeveloperSettings; callers still treat null defensively.
	return Settings;
}

// =====================================================================================================
// Debug
// =====================================================================================================

FString UMod_ContentManagerSubsystem::GetDPDebugString_Implementation() const
{
	int32 Mounted = 0, Discovered = 0, Rejected = 0;
	for (const TPair<FGameplayTag, FMod_MountedPack>& Pair : Packs)
	{
		switch (Pair.Value.State)
		{
		case EMod_PackState::Mounted:    ++Mounted; break;
		case EMod_PackState::Discovered: ++Discovered; break;
		case EMod_PackState::Rejected:   ++Rejected; break;
		default: break;
		}
	}

	const UMod_DeveloperSettings* Settings = GetSettings();
	const TCHAR* AutoStr = (Settings && Settings->AutoMountPolicy == EMod_AutoMountPolicy::OnStartup) ? TEXT("OnStartup") : TEXT("Off");

	return FString::Printf(TEXT("ModContent: mounted=%d discovered=%d rejected=%d | sources=%d | auto=%s | pak=%s"),
		Mounted, Discovered, Rejected, WeakSources.Num(), AutoStr,
		bPakMountingAvailable ? TEXT("avail") : TEXT("n/a"));
}
