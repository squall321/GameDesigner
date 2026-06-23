// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Resolve/Mod_DependencyResolverSubsystem.h"
#include "Manager/Mod_ContentManagerSubsystem.h"
#include "Registry/Mod_ContentRegistrySubsystem.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Engine/GameInstance.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UMod_DependencyResolverSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Verbose, TEXT("ModContent: dependency resolver initialised (diagnostics planner)."));
}

void UMod_DependencyResolverSubsystem::Deinitialize()
{
	LastPlan = FMod_LoadOrderPlan();
	Super::Deinitialize();
}

// =====================================================================================================
// Version satisfaction
// =====================================================================================================

FMod_SemVer UMod_DependencyResolverSubsystem::GetPackVersion(const FMod_PackInfo& Pack)
{
	if (const UMod_ContentPackDescriptor* Desc = Pack.Descriptor.Get())
	{
		return Desc->PackVersion;
	}
	return FMod_SemVer(); // no descriptor -> treated as version 0 (unset)
}

bool UMod_DependencyResolverSubsystem::SatisfiesVersion(
	const FMod_PackDependency& Dependency, const TArray<FMod_PackInfo>& Available) const
{
	if (!Dependency.DependencyId.IsValid())
	{
		// A malformed dependency id cannot be satisfied; treat optional as satisfied, hard as unsatisfied.
		return Dependency.bOptional;
	}

	const FMod_PackInfo* Found = Available.FindByPredicate(
		[&Dependency](const FMod_PackInfo& P) { return P.PackId == Dependency.DependencyId; });

	if (Found == nullptr)
	{
		// Absent: optional deps are considered satisfied (they only warn); hard deps are not.
		return Dependency.bOptional;
	}

	// Present: must meet the minimum version (zero floor is satisfied by anything).
	const FMod_SemVer Version = GetPackVersion(*Found);
	return Dependency.MinVersion.IsZero() || Version.IsAtLeast(Dependency.MinVersion);
}

// =====================================================================================================
// Plan building (read-only mirror of the manager's topological order)
// =====================================================================================================

FMod_LoadOrderPlan UMod_DependencyResolverSubsystem::BuildPlan(const TArray<FMod_PackInfo>& Discovered)
{
	FMod_LoadOrderPlan Plan;

	// Index packs by id and seed a node per pack.
	TMap<FGameplayTag, const FMod_PackInfo*> ById;
	for (const FMod_PackInfo& Pack : Discovered)
	{
		if (Pack.PackId.IsValid())
		{
			ById.Add(Pack.PackId, &Pack);
		}
	}

	// Build the dependency edges (Dep -> Pack, "Dep mounts before Pack") and in-degrees over HARD,
	// PRESENT, version-satisfied dependencies only. Record per-node unsatisfied hard deps as diagnostics.
	TMap<FGameplayTag, int32> InDegree;
	TMap<FGameplayTag, TArray<FGameplayTag>> Dependents;
	TMap<FGameplayTag, FMod_PlanNode> NodeById;

	for (const TPair<FGameplayTag, const FMod_PackInfo*>& Pair : ById)
	{
		FMod_PlanNode Node;
		Node.PackId = Pair.Key;
		NodeById.Add(Pair.Key, Node);
		InDegree.FindOrAdd(Pair.Key, 0);
	}

	for (const TPair<FGameplayTag, const FMod_PackInfo*>& Pair : ById)
	{
		const FMod_PackInfo* Pack = Pair.Value;
		const UMod_ContentPackDescriptor* Desc = Pack->Descriptor.Get();
		if (!Desc)
		{
			continue;
		}

		FMod_PlanNode& Node = NodeById.FindChecked(Pair.Key);
		for (const FMod_PackDependency& Dep : Desc->Dependencies)
		{
			if (!Dep.DependencyId.IsValid())
			{
				continue;
			}

			const bool bPresent = ById.Contains(Dep.DependencyId);
			const bool bSatisfied = SatisfiesVersion(Dep, Discovered);

			if (!bSatisfied)
			{
				// Hard-unsatisfied (missing or too old). Optional unsatisfied deps only warn -> not recorded
				// as a blocker, but a hard one makes the node un-orderable.
				if (!Dep.bOptional)
				{
					Node.UnsatisfiedDeps.AddUnique(Dep.DependencyId);
				}
				continue;
			}

			if (bPresent)
			{
				// Edge Dep -> Pack.
				InDegree.FindOrAdd(Pair.Key)++;
				Dependents.FindOrAdd(Dep.DependencyId).Add(Pair.Key);
			}
		}
	}

	// Nodes with a hard-unsatisfied dependency cannot be placed: mark them and remove from ordering.
	TSet<FGameplayTag> Unorderable;
	for (const TPair<FGameplayTag, FMod_PlanNode>& Pair : NodeById)
	{
		if (Pair.Value.UnsatisfiedDeps.Num() > 0)
		{
			Unorderable.Add(Pair.Key);
		}
	}

	// Kahn's algorithm with a deterministic alphabetical tie-break among ready nodes.
	TArray<FGameplayTag> Ready;
	for (const TPair<FGameplayTag, int32>& Pair : InDegree)
	{
		if (Pair.Value == 0 && !Unorderable.Contains(Pair.Key))
		{
			Ready.Add(Pair.Key);
		}
	}
	Ready.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().LexicalLess(B.GetTagName()); });

	int32 NextOrder = 0;
	int32 Placed = 0;
	const int32 PlaceableCount = NodeById.Num() - Unorderable.Num();

	while (Ready.Num() > 0)
	{
		const FGameplayTag Current = Ready[0];
		Ready.RemoveAt(0);

		NodeById.FindChecked(Current).OrderIndex = NextOrder++;
		++Placed;

		if (const TArray<FGameplayTag>* Outs = Dependents.Find(Current))
		{
			TArray<FGameplayTag> NewlyReady;
			for (const FGameplayTag& Dependent : *Outs)
			{
				if (Unorderable.Contains(Dependent))
				{
					continue;
				}
				int32& Deg = InDegree.FindChecked(Dependent);
				if (--Deg == 0)
				{
					NewlyReady.Add(Dependent);
				}
			}
			if (NewlyReady.Num() > 0)
			{
				NewlyReady.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().LexicalLess(B.GetTagName()); });
				Ready.Append(NewlyReady);
			}
		}
	}

	// Any placeable node still un-placed is part of a cycle.
	if (Placed < PlaceableCount)
	{
		for (TPair<FGameplayTag, FMod_PlanNode>& Pair : NodeById)
		{
			if (Pair.Value.OrderIndex == INDEX_NONE && !Unorderable.Contains(Pair.Key))
			{
				Pair.Value.bInCycle = true;
			}
		}
	}

	// Emit nodes (placed nodes first by order, then the rest).
	NodeById.GenerateValueArray(Plan.Nodes);
	Plan.Nodes.Sort([](const FMod_PlanNode& A, const FMod_PlanNode& B)
	{
		const int32 Oa = (A.OrderIndex == INDEX_NONE) ? MAX_int32 : A.OrderIndex;
		const int32 Ob = (B.OrderIndex == INDEX_NONE) ? MAX_int32 : B.OrderIndex;
		if (Oa != Ob) { return Oa < Ob; }
		return A.PackId.GetTagName().LexicalLess(B.PackId.GetTagName());
	});

	Plan.bFullyResolved = (Unorderable.Num() == 0) && (Placed == PlaceableCount) && (PlaceableCount == NodeById.Num());

	LastPlan = Plan;
	return Plan;
}

FMod_LoadOrderPlan UMod_DependencyResolverSubsystem::BuildPlanFromManager()
{
	if (const UMod_ContentManagerSubsystem* Manager = ResolveManager())
	{
		TArray<FMod_PackInfo> Infos;
		for (const FMod_MountedPack& Rec : Manager->GetAllPacks())
		{
			Infos.Add(Rec.Info);
		}
		return BuildPlan(Infos);
	}
	UE_LOG(LogDP, Verbose, TEXT("ModContent: BuildPlanFromManager found no manager; empty plan."));
	return FMod_LoadOrderPlan();
}

// =====================================================================================================
// Conflict detection
// =====================================================================================================

FMod_ConflictReport UMod_DependencyResolverSubsystem::DetectConflicts(const TArray<FMod_PackInfo>& Discovered)
{
	FMod_ConflictReport Report;

	// Pre-mount conflict signal: which packs declare an overlapping content ROOT prefix. A shared root
	// prefix means two packs contribute under the same virtual namespace, the structural precondition for
	// a data-tag override clash. We surface these as conflicts keyed by the shared root (as a name tag is
	// not available pre-load, the conflict's DataTag is left invalid and the root carried in diagnostics
	// via the contender set). This reasons purely over descriptor metadata (load-free).
	TMap<FString, TArray<FGameplayTag>> RootToPacks;
	for (const FMod_PackInfo& Pack : Discovered)
	{
		const UMod_ContentPackDescriptor* Desc = Pack.Descriptor.Get();
		if (!Desc || !Pack.PackId.IsValid())
		{
			continue;
		}
		for (const FString& Root : Desc->ContentRoots)
		{
			if (!Root.IsEmpty())
			{
				RootToPacks.FindOrAdd(Root).AddUnique(Pack.PackId);
			}
		}
	}

	// Additionally, when a registry is live, fold in ACTUAL overridden-tag conflicts (post-mount truth).
	const UMod_ContentRegistrySubsystem* Registry = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		Registry = GI->GetSubsystem<UMod_ContentRegistrySubsystem>();
	}

	const TScriptInterface<ISeam_ModResolutionPolicy> Policy = ResolvePolicy();

	// Helper to resolve and append one conflict.
	auto AppendConflict = [&Report, &Policy](FGameplayTag DataTag, const TArray<FGameplayTag>& Contenders, FGameplayTag DefaultWinner)
	{
		if (Contenders.Num() < 2)
		{
			return;
		}
		FMod_ConflictEntry Entry;
		Entry.Conflict.DataTag = DataTag;
		Entry.Conflict.Contenders = Contenders;
		Entry.Conflict.DefaultWinner = DefaultWinner;

		FGameplayTag Winner = DefaultWinner;
		if (Policy.GetObject())
		{
			const FGameplayTag PolicyWinner = ISeam_ModResolutionPolicy::Execute_ResolveConflict(Policy.GetObject(), Entry.Conflict);
			if (Contenders.Contains(PolicyWinner))
			{
				Winner = PolicyWinner; // honour the policy only if it picks a real contender
			}
		}
		Entry.ResolvedWinner = Winner;
		Report.Conflicts.Add(MoveTemp(Entry));
	};

	// Root-overlap conflicts (no concrete DataTag; DefaultWinner = first declarer for determinism).
	for (const TPair<FString, TArray<FGameplayTag>>& Pair : RootToPacks)
	{
		if (Pair.Value.Num() >= 2)
		{
			AppendConflict(FGameplayTag(), Pair.Value, Pair.Value[0]);
		}
	}

	// Actual data-tag override conflicts from the live registry, if present.
	if (Registry)
	{
		const TArray<FGameplayTag> Tags = Registry->ListOverriddenTags();
		for (const FGameplayTag& Tag : Tags)
		{
			const TArray<FMod_AssetOverride> Chain = Registry->GetOverridesForTag(Tag);
			if (Chain.Num() >= 2)
			{
				TArray<FGameplayTag> Contenders;
				for (const FMod_AssetOverride& Ov : Chain)
				{
					Contenders.AddUnique(Ov.SourcePackId);
				}
				// Chain[0] is the registry's current winner (precedence-sorted by GetOverridesForTag).
				AppendConflict(Tag, Contenders, Chain[0].SourcePackId);
			}
		}
	}

	return Report;
}

// =====================================================================================================
// Resolution helpers / debug
// =====================================================================================================

UMod_ContentManagerSubsystem* UMod_DependencyResolverSubsystem::ResolveManager() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UMod_ContentManagerSubsystem>();
	}
	return nullptr;
}

UDP_ServiceLocatorSubsystem* UMod_DependencyResolverSubsystem::GetLocator() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	}
	return nullptr;
}

TScriptInterface<ISeam_ModResolutionPolicy> UMod_DependencyResolverSubsystem::ResolvePolicy() const
{
	TScriptInterface<ISeam_ModResolutionPolicy> Result;
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

FString UMod_DependencyResolverSubsystem::GetDPDebugString_Implementation() const
{
	int32 Cycles = 0;
	int32 Unsatisfied = 0;
	for (const FMod_PlanNode& Node : LastPlan.Nodes)
	{
		if (Node.bInCycle) { ++Cycles; }
		if (Node.UnsatisfiedDeps.Num() > 0) { ++Unsatisfied; }
	}
	return FString::Printf(TEXT("DependencyResolver: %d node(s), resolved=%s, %d cycle(s), %d unsatisfied"),
		LastPlan.Nodes.Num(), LastPlan.bFullyResolved ? TEXT("yes") : TEXT("no"), Cycles, Unsatisfied);
}
