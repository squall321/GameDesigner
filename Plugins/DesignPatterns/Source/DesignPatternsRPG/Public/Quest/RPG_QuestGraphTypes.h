// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Quest/RPG_QuestDefinition.h"   // FRPG_QuestObjective (wrapped by FRPG_QuestStageObjective)
#include "RPG_QuestGraphTypes.generated.h"

class URPG_Objective;

/**
 * Comparison operator used by FRPG_StageGate counter checks.
 *
 * Mirrors ENarr_CounterCompare so a designer used to the narrative gate vocabulary sees the same set,
 * but it is RPG-LOCAL so the RPG quest graph never includes a Narrative header (the rule-3 fix: quest
 * branching uses RPG-local predicates/effects, not UNarr_Condition / UNarr_Effect).
 */
UENUM(BlueprintType)
enum class ERPG_CounterCompare : uint8
{
	Less,
	LessEqual,
	Equal,
	GreaterEqual,
	Greater,
	NotEqual
};

/**
 * How a stage's objective set combines into "stage complete".
 *
 *  - All: every REQUIRED (non-optional) objective must be satisfied (the classic AND quest stage).
 *  - Any: at least one objective (optional or required) being satisfied completes the stage (an OR
 *    branch point — e.g. "sneak past OR fight through").
 */
UENUM(BlueprintType)
enum class ERPG_StageLogic : uint8
{
	/** AND: all required objectives must complete. */
	All,
	/** OR: any single objective completing finishes the stage. */
	Any
};

/** Which world-hub scope an FRPG_HubWrite addresses, kept RPG-local (no World enum in a public header). */
UENUM(BlueprintType)
enum class ERPG_HubScopeKind : uint8
{
	/** A single value shared by the whole session/world. */
	Global,
	/** A value owned by a faction (FactionTag supplied by the tracker context). */
	Faction,
	/** A value owned by the quest owner's entity id (resolved via the identity seam in .cpp). */
	OwnerEntity
};

/**
 * An RPG-local predicate evaluated against the world hub + the reputation seam.
 *
 * This REPLACES UNarr_Condition inside the quest graph so the RPG module never hard-includes Narrative.
 * Every field is optional: an unset HubFlagKey/CounterKey/RequiredPriorStage/ReputationTag simply does
 * not contribute a sub-check, so an all-default gate passes (vacuous truth). The tracker evaluates this
 * in .cpp by resolving the concrete UWorldHub_StateHubSubsystem (typed reads) and the ISeam_Reputation
 * provider; both fail closed (the missing backend's sub-check evaluates false) so a gate never silently
 * "passes" because its data source was absent.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_StageGate
{
	GENERATED_BODY()

	/** Optional hub flag the gate requires; unset = no flag sub-check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate", meta = (Categories = "DP.WorldHub"))
	FGameplayTag HubFlagKey;

	/** The value HubFlagKey must hold for the flag sub-check to pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate")
	bool bFlagExpected = true;

	/** Optional hub counter the gate compares; unset = no counter sub-check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate", meta = (Categories = "DP.WorldHub"))
	FGameplayTag CounterKey;

	/** The comparison applied as (CounterValue <op> CounterThreshold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate")
	ERPG_CounterCompare CounterCompare = ERPG_CounterCompare::GreaterEqual;

	/** Right-hand side of the counter comparison. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate")
	int64 CounterThreshold = 1;

	/** Optional stage that must already have been the quest's current/visited stage; unset = no check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate", meta = (Categories = "RPG.Quest.Stage"))
	FGameplayTag RequiredPriorStage;

	/** Optional faction/NPC standing the quest owner must meet; unset = no reputation sub-check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate")
	FGameplayTag ReputationTag;

	/** Minimum standing required with ReputationTag (only meaningful when ReputationTag is set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Gate")
	int32 MinReputation = 0;

	/** @return true if this gate carries no sub-checks at all (always-pass). */
	bool IsEmpty() const
	{
		return !HubFlagKey.IsValid() && !CounterKey.IsValid()
			&& !RequiredPriorStage.IsValid() && !ReputationTag.IsValid();
	}
};

/**
 * A single authoritative world-hub write applied when a branch outcome fires.
 *
 * REPLACES UNarr_Effect_SetFlag / _AddCounter for the RPG side: the tracker applies it via the concrete
 * UWorldHub_StateHubSubsystem in .cpp, authority-only (the hub mutators themselves no-op on clients, and
 * the tracker is server-authoritative anyway). State therefore lives in / feeds the world hub, replicating
 * and saving through the hub's single path.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_HubWrite
{
	GENERATED_BODY()

	/** The hub flag or counter key to write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect", meta = (Categories = "DP.WorldHub"))
	FGameplayTag Key;

	/** When true Key is a counter (CounterDelta is added); when false Key is a flag (bFlagValue is set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect")
	bool bIsCounter = false;

	/** Flag value to set (only when bIsCounter is false). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect", meta = (EditCondition = "!bIsCounter"))
	bool bFlagValue = true;

	/** Counter delta to add (only when bIsCounter is true; may be negative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect", meta = (EditCondition = "bIsCounter"))
	int64 CounterDelta = 0;

	/** Which scope to write under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect")
	ERPG_HubScopeKind ScopeKind = ERPG_HubScopeKind::Global;

	/** Faction this write targets when ScopeKind == Faction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Effect", meta = (EditCondition = "ScopeKind == ERPG_HubScopeKind::Faction"))
	FGameplayTag FactionTag;
};

/**
 * One objective slot within a stage.
 *
 * Wraps the REAL FRPG_QuestObjective (so the quest log's per-objective counter machinery is reused
 * unchanged) and adds branching-layer metadata: optional/hidden flags plus an inline, instanced
 * URPG_Objective tracker that knows HOW the objective makes progress (kill N tag, collect N item,
 * reach a location, talk, escort, defend). No Narrative types appear here.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestStageObjective
{
	GENERATED_BODY()

	/** The underlying counter objective (ObjectiveTag + Description + RequiredCount) tracked by the log. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	FRPG_QuestObjective Objective;

	/** When true this objective is not required for stage completion (bonus / side objective). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	bool bOptional = false;

	/** When true this objective is hidden from the tracker UI until revealed (a secret objective). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	bool bHidden = false;

	/**
	 * The inline evaluator that observes the bus/hub and reports deltas to the tracker. Instanced so each
	 * objective slot owns its own configured tracker. Null = a manually-advanced objective (gameplay code
	 * calls the tracker's ReportProgress directly).
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	TObjectPtr<URPG_Objective> Tracker;
};

/**
 * A branch outcome attached to a stage: when its gate passes, route the quest to NextStage (or
 * complete/fail it) and apply its hub-write effects.
 *
 * Outcomes are evaluated in array order each time the stage's objectives change; the FIRST whose When
 * gate passes fires. This is what makes the quest a graph rather than a line: the same stage can route to
 * different next stages depending on world state / reputation / which objective the player completed.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestBranchOutcome
{
	GENERATED_BODY()

	/** Gate that must pass for this outcome to fire (empty gate = fires as soon as the stage completes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Outcome")
	FRPG_StageGate When;

	/** The stage to advance to. Unset (and not completing/failing) ends the graph at this stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Outcome", meta = (Categories = "RPG.Quest.Stage"))
	FGameplayTag NextStage;

	/** Authoritative hub writes applied (in order) when this outcome fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Outcome")
	TArray<FRPG_HubWrite> Effects;

	/** When true firing this outcome completes the whole quest (overrides NextStage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Outcome")
	bool bCompletesQuest = false;

	/** When true firing this outcome fails the whole quest (overrides NextStage / bCompletesQuest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Outcome")
	bool bFailsQuest = false;
};

/**
 * One stage of a branching quest: an AND/OR objective set with prerequisites, an optional time limit, a
 * set of branch outcomes, and an optional fail-to stage.
 *
 * Pure immutable content (lives on the URPG_QuestGraphDefinition data asset). All runtime stage state
 * (which stage is current, which hidden objectives are revealed, when the stage started) lives on the
 * tracker component's replicated fast-array, never here.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestStage
{
	GENERATED_BODY()

	/** Stable identity of this stage within the graph (e.g. RPG.Quest.Stage.InvestigateRuins). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage", meta = (Categories = "RPG.Quest.Stage"))
	FGameplayTag StageTag;

	/** Player-facing stage title for the tracker UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	FText StageTitle;

	/** How this stage's objectives combine into completion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	ERPG_StageLogic Completion = ERPG_StageLogic::All;

	/** The objectives of this stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	TArray<FRPG_QuestStageObjective> Objectives;

	/** Gates that must ALL pass before this stage may be entered (checked by the tracker on advance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	TArray<FRPG_StageGate> Prerequisites;

	/** Seconds the player has to complete this stage; 0 = no limit. Anchored to the saved stage start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage", meta = (ClampMin = "0.0"))
	float TimeLimitSeconds = 0.f;

	/** Branch outcomes evaluated (in order) when the stage's objectives change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage")
	TArray<FRPG_QuestBranchOutcome> Outcomes;

	/** Stage to route to if this stage's time limit elapses; unset = fail the quest on timeout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Stage", meta = (Categories = "RPG.Quest.Stage"))
	FGameplayTag FailToStage;
};
