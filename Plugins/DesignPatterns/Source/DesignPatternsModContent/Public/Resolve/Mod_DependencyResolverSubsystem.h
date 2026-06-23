// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Seam/Mod_ContentSource.h"             // FMod_PackInfo
#include "Descriptor/Mod_ContentPackDescriptor.h" // FMod_PackDependency, FMod_SemVer
#include "Mod/Seam_ModResolution.h"             // ISeam_ModResolutionPolicy, FMod_TagConflict
#include "Mod_DependencyResolverSubsystem.generated.h"

class UMod_ContentManagerSubsystem;
class UDP_ServiceLocatorSubsystem;

/**
 * One node in a computed load-order plan: a pack, its resolved order index, and any diagnostics found
 * while planning (unsatisfied dependencies, cycle membership). PURE DATA for load-order UI/diagnostics —
 * PII-free, never replicated. Mirrors the manager's FMod_MountedPack.OrderIndex semantics (lower mounts
 * first; INDEX_NONE means "could not be placed").
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PlanNode
{
	GENERATED_BODY()

	/** The pack this node represents (child of DP.Mod.Pack). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	FGameplayTag PackId;

	/** Resolved topological order index (lower mounts first). INDEX_NONE when the pack cannot be placed. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	int32 OrderIndex = INDEX_NONE;

	/** Declared dependency ids that are missing, too old, or part of a cycle (hard deps only). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	TArray<FGameplayTag> UnsatisfiedDeps;

	/** True when this pack is part of a dependency cycle (which makes it un-orderable). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	bool bInCycle = false;
};

/**
 * The full computed load-order plan: every node in resolved order plus a flag for whether the plan is
 * mountable as a whole (no cycles, no hard-unsatisfied deps). Diagnostics only; never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_LoadOrderPlan
{
	GENERATED_BODY()

	/** Plan nodes. Orderable nodes carry an ascending OrderIndex; un-orderable nodes carry INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	TArray<FMod_PlanNode> Nodes;

	/** True when every node was placed (no cycle, no hard-unsatisfied dependency). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	bool bFullyResolved = false;

	/** Find a node by pack id (const). Returns nullptr if absent. */
	const FMod_PlanNode* FindNode(FGameplayTag PackId) const
	{
		return Nodes.FindByPredicate([PackId](const FMod_PlanNode& N) { return N.PackId == PackId; });
	}
};

/**
 * A detected data-tag override conflict between two or more packs in the discovered/planned set, plus
 * the winner the active conflict policy chose. Diagnostics only. Wraps the seam FMod_TagConflict with a
 * resolved winner for UI.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_ConflictEntry
{
	GENERATED_BODY()

	/** The seam-facing conflict description (contended tag + contenders + default winner). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	FMod_TagConflict Conflict;

	/** The winner after consulting the policy seam (or the default winner when no policy is registered). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	FGameplayTag ResolvedWinner;
};

/**
 * The aggregate conflict report for a discovered/planned set: every detected data-tag conflict with its
 * resolved winner. Diagnostics only; never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_ConflictReport
{
	GENERATED_BODY()

	/** Every detected conflict (empty when no two packs override the same tag). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Resolve")
	TArray<FMod_ConflictEntry> Conflicts;

	/** True when at least one conflict was detected. */
	bool HasConflicts() const { return Conflicts.Num() > 0; }
};

/**
 * STANDALONE depth / diagnostics subsystem for the mod dependency graph.
 *
 * It re-derives the version-constrained dependency graph from the manager's PUBLIC GetAllPacks() (or a
 * caller-supplied FMod_PackInfo set), producing an FMod_LoadOrderPlan + FMod_ConflictReport for
 * load-order UI and "why won't this pack mount?" tooling. It does NOT replace the manager's authoritative
 * ResolveMountOrder — the manager still owns mount ordering; this is a read-only planner that mirrors the
 * same rules (topological sort, cycle detection, version floors) so UI and the manager agree.
 *
 * It consults an optional ISeam_ModResolutionPolicy (DP.Service.Mod.Resolution), held WEAKLY and pruned
 * on use, only to RANK and pick conflict winners; with no policy the inert default keeps the registry's
 * shipped precedence. GameInstance-scoped, non-replicated, non-saved.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_DependencyResolverSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Build a load-order plan over an explicit discovered set. Runs a topological sort over hard
	 * dependencies (honouring version floors), detects cycles, and records per-node diagnostics. Pure;
	 * stores the result as the last plan. Returns the plan by value.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Resolve")
	FMod_LoadOrderPlan BuildPlan(const TArray<FMod_PackInfo>& Discovered);

	/**
	 * Convenience: build a plan from the manager's current GetAllPacks() set (the live discovered packs).
	 * Returns an empty plan when the manager is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Resolve")
	FMod_LoadOrderPlan BuildPlanFromManager();

	/**
	 * Detect data-tag override conflicts across the discovered set: where two or more packs' descriptors
	 * declare an override for the same data tag. Resolves each via the policy seam (or the default
	 * precedence). Pure. Note: this reasons over descriptor-declared overrides, so it works pre-mount.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Resolve")
	FMod_ConflictReport DetectConflicts(const TArray<FMod_PackInfo>& Discovered);

	/** The plan computed by the most recent BuildPlan/BuildPlanFromManager call. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Resolve")
	const FMod_LoadOrderPlan& GetLastPlan() const { return LastPlan; }

	/**
	 * True if Dependency is satisfied by some pack in Available (present AND at least MinVersion). Used by
	 * BuildPlan and exposed for tooling that wants to test a single dependency.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Resolve")
	bool SatisfiesVersion(const FMod_PackDependency& Dependency, const TArray<FMod_PackInfo>& Available) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the manager subsystem (GI sibling). Null in early/teardown contexts. */
	UMod_ContentManagerSubsystem* ResolveManager() const;

	/** Resolve the optional resolution policy seam (weak, locator-backed). Null when none registered. */
	TScriptInterface<ISeam_ModResolutionPolicy> ResolvePolicy() const;

	/** Resolve the service locator (may be null in early/teardown contexts). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** The pack's own version, read from its descriptor (zero when no descriptor / unset). */
	static FMod_SemVer GetPackVersion(const FMod_PackInfo& Pack);

	/** The most recently computed plan (diagnostics cache). */
	UPROPERTY(Transient)
	FMod_LoadOrderPlan LastPlan;
};
