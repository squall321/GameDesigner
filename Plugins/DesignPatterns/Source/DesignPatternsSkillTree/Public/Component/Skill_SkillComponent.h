// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "Component/Skill_LearnedArray.h"
#include "Component/Skill_LearnedRecord.h"
#include "Skill_SkillComponent.generated.h"

class USkill_SkillComponent;
class USkill_SkillDefinition;
class USkill_SkillTreeDefinition;

/**
 * Native machine-readable failure-reason tags carried by FSkill_LearnEval::FailureReason when a learn is
 * denied. These are *reason ids*, not gameplay tunables — they let UI map a denial to a localized tooltip
 * without string parsing, and let the authoritative server and any client agree on why a node is blocked.
 * They live in this component's area because the evaluator that produces them lives here.
 */
namespace SkillFailureTags
{
	/** The skill tag did not resolve to any authored USkill_SkillDefinition in the data registry. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Unknown);

	/** The node is already at its authored (or settings-capped) MaxRank — nothing left to learn. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_MaxRank);

	/** A prerequisite node is not learned to its required rank. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Prereq);

	/** A mutually-exclusive sibling in the node's MutexGroup is already learned. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Mutex);

	/** The owner has not spent enough points to unlock this node's tier. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Tier);

	/** The owner's level (from ISkill_PointSource) is below the node's required level. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Level);

	/** The available point pool does not cover this step's point cost. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Failure_Cost);
}

/**
 * Result of evaluating whether a skill may be learned/ranked-up. PURE + client-safe: built from the
 * replicated learned state + the (designer-authored, non-replicated but locally available) skill
 * definition data, so UI on any client can grey-out / explain an unavailable skill identically to the
 * server's authoritative check. The server re-runs CanLearn before mutating, so a lying client gains nothing.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_LearnEval
{
	GENERATED_BODY()

	/** True if the skill can be learned (or ranked up) right now by this owner. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree")
	bool bAllowed = false;

	/** The rank that would result from a successful learn (current rank + 1). */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree")
	int32 ResultingRank = 0;

	/** Point cost of this specific learn/rank-up step. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree")
	int32 PointCost = 0;

	/** Machine-readable reason tag when bAllowed is false (e.g. failure: prereq / cost / mutex / tier / level). */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree")
	FGameplayTag FailureReason;

	FSkill_LearnEval() = default;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSkill_OnSkillLearned, FGameplayTag, SkillTag, int32, NewRank);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSkill_OnPointsChanged, int32, AvailablePoints);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkill_OnRespec);

/**
 * Replicated skill component: the authoritative owner of a character's learned skills and earned points.
 *
 * Responsibilities
 *  - Holds the replicated FSkill_LearnedArray (delta-replicated learned entries) and the replicated
 *    TotalEarnedPoints scalar.
 *  - AUTHORITY-ONLY mutators LearnSkill / RankUp / Respec, each gated by the PURE, client-safe CanLearn
 *    (prereqs / mutex group / tier-unlock / point cost / character level). Clients route intent through a
 *    player-owned request component (see Skill_SkillRequestComponent) whose Server_* RPC calls these here;
 *    the server always re-derives the eval, so client state is advisory only.
 *  - On a successful learn it resolves an ISkill_AbilityGranter off the owning actor (Implements<> or a
 *    component scan) and grants the skill's linked ability. If no granter is present the learn still
 *    succeeds (the grant is simply skipped) — a documented inert-default degrade.
 *  - For the point budget it resolves an ISkill_PointSource off the owner; if none is present it falls
 *    back to the editable FallbackPointBudget tunable.
 *
 * Replication policy (HARD RULES 3 & 5): replicated state lives here on a UActorComponent (never on a
 * subsystem); every mutator guards HasAuthority() at the top and early-returns on clients. Reads
 * (GetSkillRank / GetAvailablePoints / GetLearnedSkills / CanLearn) are local and run on any client.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSKILLTREE_API USkill_SkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USkill_SkillComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void InitializeComponent() override;
	//~ End UActorComponent

	// ---- Authority-only mutators ----------------------------------------------------------------

	/**
	 * Learn a skill at rank 1 (or, if already learned, behave like RankUp). AUTHORITY ONLY: guards
	 * HasAuthority() at the top and early-returns on clients. Runs CanLearn; on success records the entry,
	 * spends the points, resolves + invokes the ability granter for the linked ability, and broadcasts.
	 * @return true if the skill was learned/ranked this call.
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree")
	bool LearnSkill(FGameplayTag SkillTag);

	/** Increase an already-learned skill's rank by one. AUTHORITY ONLY. Same gating/effects as LearnSkill. */
	UFUNCTION(BlueprintCallable, Category = "SkillTree")
	bool RankUp(FGameplayTag SkillTag);

	/**
	 * Refund every learned skill, revoking each linked ability via the granter and resetting spent points
	 * to zero (TotalEarnedPoints is preserved — only the *spend* is undone). AUTHORITY ONLY.
	 * @return true if a respec occurred (there was at least one learned skill).
	 */
	UFUNCTION(BlueprintCallable, Category = "SkillTree")
	bool Respec();

	// ---- Local reads (client-safe) --------------------------------------------------------------

	/** Current rank of a skill (0 if not learned). Reads replicated state; valid on clients. */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	int32 GetSkillRank(FGameplayTag SkillTag) const;

	/** True if the skill is learned at rank >= 1. */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	bool HasSkill(FGameplayTag SkillTag) const { return GetSkillRank(SkillTag) > 0; }

	/** Total points the owner has earned (replicated). The denominator of the spend budget. */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	int32 GetTotalEarnedPoints() const { return TotalEarnedPoints; }

	/** Sum of the point cost of every learned rank of every learned skill. Derived locally from the array. */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	int32 GetPointsSpent() const;

	/** Points available to spend = TotalEarnedPoints - GetPointsSpent(), clamped at 0. */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	int32 GetAvailablePoints() const;

	/** Snapshot of all learned skills as plain records (also used to seed save data). */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	TArray<FSkill_LearnedRecord> GetLearnedSkills() const;

	/**
	 * PURE, client-safe evaluation of whether SkillTag can be learned/ranked-up right now. Consults the
	 * replicated learned state + the skill definition (prereqs/mutex/tier/cost) + character level. The
	 * authoritative mutators call this first; UI calls it to drive enable/disable + tooltips identically.
	 */
	UFUNCTION(BlueprintPure, Category = "SkillTree")
	FSkill_LearnEval CanLearn(FGameplayTag SkillTag) const;

	// ---- Save integration (called by USkill_SkillSaveGame) --------------------------------------

	/**
	 * AUTHORITY-ONLY import of saved skill state. Replaces the learned array and earned-point total from
	 * the records, then (re)resolves the granter and re-grants every linked ability so the restored loadout
	 * is live. Guards HasAuthority() at the top; a no-op on clients (they receive the result via replication).
	 */
	void ImportFromSave(const TArray<FSkill_LearnedRecord>& Records, int32 InTotalEarnedPoints);

	// ---- Events ---------------------------------------------------------------------------------

	/** Broadcast (locally, on both server and clients) when a skill is learned or ranked up. */
	UPROPERTY(BlueprintAssignable, Category = "SkillTree")
	FSkill_OnSkillLearned OnSkillLearned;

	/** Broadcast (locally) whenever the available point budget changes (earn, spend, respec, replication). */
	UPROPERTY(BlueprintAssignable, Category = "SkillTree")
	FSkill_OnPointsChanged OnPointsChanged;

	/** Broadcast (locally) when a respec completes. */
	UPROPERTY(BlueprintAssignable, Category = "SkillTree")
	FSkill_OnRespec OnRespec;

	// ---- Tunables -------------------------------------------------------------------------------

	/**
	 * Defensive fallback point budget used as TotalEarnedPoints when NO ISkill_PointSource can be resolved
	 * off the owner. A documented inert default so the tree is still usable in isolation; designers set the
	 * real budget through a point-source adapter (USkill_BudgetPointSource) or a ISkill_PointSource provider.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Tuning", meta = (ClampMin = "0"))
	int32 FallbackPointBudget = 0;

	/**
	 * The point channel this component spends from. Passed to the resolved ISkill_PointSource so a single
	 * source can budget multiple trees (e.g. combat vs crafting). Empty = the source's default channel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Tuning")
	FGameplayTag PointChannelTag;

	/**
	 * Points the owner must have already spent in this tree to unlock a node at tier N, expressed as a
	 * per-tier requirement: required-spend(node) = node.Tier * RequiredSpentPerTier. 0 (default) disables
	 * tier-spend gating entirely (every tier is reachable from the start). This is the genre-neutral tunable
	 * that turns the authored, otherwise layout-only node Tier into a progression gate WITHOUT baking a
	 * magic number into the evaluator.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Tuning", meta = (ClampMin = "0"))
	int32 RequiredSpentPerTier = 0;

	/**
	 * Owner level required to unlock a node at tier N, expressed per tier: required-level(node) =
	 * node.Tier * RequiredLevelPerTier. The owner level is read through the resolved ISkill_PointSource
	 * (0 when none resolves => ungated). 0 (default) disables level gating. Tunable, not a magic number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Tuning", meta = (ClampMin = "0"))
	int32 RequiredLevelPerTier = 0;

	/**
	 * Optional explicit skill tree this component is seeded from. When set, CanLearn/GetPointsSpent resolve
	 * node definitions from this tree first (fast, in-memory) before falling back to the data registry, so a
	 * tree whose nodes are not separately registered still works. Soft-resolved lazily; leave unset to rely
	 * purely on the registry (nodes addressed by their DataTag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Tuning")
	TSoftObjectPtr<USkill_SkillTreeDefinition> SkillTree;

	// ---- Fast-array change hooks (called by FSkill_LearnedArray entries on clients) --------------

	/** Invoked by a learned entry on clients after an add/change; fires OnSkillLearned + OnPointsChanged. */
	void NotifyLearnedEntryChanged(const FGameplayTag& SkillTag, int32 NewRank);

	/** Invoked by a learned entry on clients before a remove (respec); fires OnPointsChanged on next tick. */
	void NotifyLearnedEntryRemoved(const FGameplayTag& SkillTag);

protected:
	/** Replicated learned-skill entries (delta-serialized). The component back-pointer is wired in the ctor. */
	UPROPERTY(Replicated)
	FSkill_LearnedArray Learned;

	/** Total points earned by the owner. Server-authoritative; drives GetAvailablePoints with GetPointsSpent. */
	UPROPERTY(ReplicatedUsing = OnRep_TotalEarnedPoints)
	int32 TotalEarnedPoints = 0;

	/** OnRep for TotalEarnedPoints: re-broadcasts OnPointsChanged so client UI updates on budget changes. */
	UFUNCTION()
	void OnRep_TotalEarnedPoints();

private:
	/** True on the network authority (dedicated/listen server or standalone). All mutators guard on this. */
	bool HasAuthorityToMutate() const;

	/**
	 * Core authoritative learn step shared by LearnSkill/RankUp: evaluates CanLearn, mutates the array,
	 * marks it dirty, resolves+invokes the ability granter, and broadcasts. Assumes authority + that the
	 * caller already early-returned for clients. Returns the resulting rank (0 on failure).
	 */
	int32 ApplyLearnAuthoritative(const FGameplayTag& SkillTag);

	/** Resolve an ISkill_AbilityGranter off the owning actor (Implements<> then component scan); may be null. */
	TScriptInterface<class ISkill_AbilityGranter> ResolveAbilityGranter() const;

	/** Resolve an ISkill_PointSource off the owning actor; may be null (=> FallbackPointBudget). */
	TScriptInterface<class ISkill_PointSource> ResolvePointSource() const;

	/** Pull the current total point budget from the resolved point source, or FallbackPointBudget if none. */
	int32 ResolveEarnedPointBudget() const;

	/** Re-sync TotalEarnedPoints from the resolved point source (authority only) and broadcast on change. */
	void RefreshEarnedPointsFromSource();

	/**
	 * Resolve a node's authored definition: from the (optionally set) SkillTree first, else the core data
	 * registry by DataTag. Definitions are designer data available identically on every peer, so this is
	 * client-safe and underpins the PURE CanLearn. Returns null when the tag names no authored node.
	 */
	USkill_SkillDefinition* ResolveDefinition(const FGameplayTag& SkillTag) const;

	/** The node's effective max rank: authored MaxRank clamped to the settings AbsoluteMaxRank safety cap. */
	int32 GetEffectiveMaxRank(const USkill_SkillDefinition& Def) const;

	/** Cumulative point cost of holding RankToCount ranks of Def (per-rank PointCost summed). */
	static int32 GetCumulativeCost(const USkill_SkillDefinition& Def, int32 RankToCount);

	/** Owner level via the resolved ISkill_PointSource (0 if none / no level concept). */
	int32 ResolveOwnerLevel() const;

	/** The linked ability tag of a learned node, or an invalid tag (no definition / passive node). */
	FGameplayTag GetLinkedAbilityTag(const FGameplayTag& SkillTag) const;
};
