// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Dialogue/Narr_DialogueTypes.h"
#include "Logic/Narr_Condition.h"   // shared UNarr_Condition (Instanced node/edge gates)
#include "Logic/Narr_Effect.h"      // shared UNarr_Effect (Instanced node effects)
#include "Narr_DialogueGraph.generated.h"

/**
 * What a single graph node does when the runner enters it.
 *
 * The runner walks the graph node-by-node. A node's Kind selects how the runner presents it and how it
 * leaves: a Line shows one line then follows its (first satisfiable) outgoing edge; a Choice presents a
 * choice set and follows the edge for the committed choice; a Condition is a silent branch that follows
 * the first edge whose guard passes; an Event runs effects/raises a beat then follows its outgoing edge.
 */
UENUM(BlueprintType)
enum class ENarr_NodeKind : uint8
{
	/** Presents one line of dialogue, then follows its first satisfiable outgoing edge. */
	Line,

	/** Presents a set of choices (one per outgoing edge that carries a ChoiceId), branches on selection. */
	Choice,

	/** Silent branch: follows the first outgoing edge whose guard condition passes. No presentation. */
	Condition,

	/** Silent: runs its effects / raises a story beat, then follows its first satisfiable outgoing edge. */
	Event
};

/**
 * One directed edge out of a node.
 *
 * For a Line/Condition/Event node the runner takes the FIRST edge (in array order) whose Guard passes.
 * For a Choice node each edge that carries a valid ChoiceId becomes a presented option; the runner takes
 * the edge whose ChoiceId matches the committed selection (its Guard determines bEnabled). An edge with an
 * invalid TargetNodeId terminates the conversation (a deliberate end-of-branch).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueEdge
{
	GENERATED_BODY()

	/** The node id this edge leads to. Invalid ends the conversation (a branch terminator). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	FGameplayTag TargetNodeId;

	/**
	 * For a Choice node: the stable id of the choice this edge represents (becomes FNarr_DialogueChoice::
	 * ChoiceId). Ignored for non-choice nodes (which pick the first satisfiable edge regardless).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	FGameplayTag ChoiceId;

	/** For a Choice edge: the localized button text shown for this option. Ignored for non-choice edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	FText ChoiceText;

	/**
	 * Optional gate. On a branch node, the edge is eligible only if this passes; on a Choice node, a
	 * failing guard renders the option disabled (bEnabled == false) rather than removing it. Null = no gate.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Graph")
	TObjectPtr<UNarr_Condition> Guard;

	FNarr_DialogueEdge() = default;

	/** @return true if this edge is a presentable choice (has a valid ChoiceId). */
	bool IsChoice() const { return ChoiceId.IsValid(); }
};

/**
 * One node of a dialogue graph.
 *
 * Identity is NodeId (unique within a graph). The node carries presentation data (Speaker + Text +
 * AutoAdvanceSeconds for a Line), an optional EntryCondition that gates whether the node is reachable at
 * all, per-node EntryEffects run on enter, and the outgoing Edges that define branching.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueNode
{
	GENERATED_BODY()

	/** Stable id of this node within its graph. Must be unique; the start node id names the entry point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	FGameplayTag NodeId;

	/** What this node does when entered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	ENarr_NodeKind Kind = ENarr_NodeKind::Line;

	/** Speaker identity for a Line node (Narr.Speaker.*). Empty = narrator/unattributed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "Kind == ENarr_NodeKind::Line"), Category = "DesignPatterns|Narrative|Graph")
	FGameplayTag Speaker;

	/** Localized line text for a Line node. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "Kind == ENarr_NodeKind::Line", MultiLine = true), Category = "DesignPatterns|Narrative|Graph")
	FText Text;

	/**
	 * Seconds after which a Line auto-advances without input. <= 0 waits for the player. Only meaningful
	 * for Line nodes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "Kind == ENarr_NodeKind::Line", ClampMin = "0.0"), Category = "DesignPatterns|Narrative|Graph")
	float AutoAdvanceSeconds = 0.f;

	/**
	 * Optional reachability gate. When set and it fails, the runner SKIPS this node: an incoming branch
	 * edge whose target is a gated-out node is treated as not eligible. Null = always reachable.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Graph")
	TObjectPtr<UNarr_Condition> EntryCondition;

	/** Effects applied (authority-side) when the runner enters this node. Authored inline. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Graph")
	TArray<TObjectPtr<UNarr_Effect>> EntryEffects;

	/** Outgoing edges. Branching semantics depend on Kind (see FNarr_DialogueEdge). */
	UPROPERTY(EditAnywhere, Category = "DesignPatterns|Narrative|Graph")
	TArray<FNarr_DialogueEdge> Edges;

	FNarr_DialogueNode() = default;

	/** @return a presentation line built from this node's Line fields. */
	FNarr_DialogueLine MakeLine() const { return FNarr_DialogueLine(Speaker, Text, AutoAdvanceSeconds); }
};

/**
 * A branching dialogue / story conversation authored as a tag-keyed data asset.
 *
 * A graph is a set of nodes plus a designated StartNodeId. The dialogue runner walks it: presenting Line
 * and Choice nodes through the local presenter seam, silently branching through Condition nodes, running
 * Effect-bearing Event/entry nodes authority-side, and gating reachability and choice availability through
 * the condition mini-language. The graph itself holds NO runtime state — it is immutable shared content;
 * all run state lives on the per-player UNarr_DialogueRunnerComponent.
 *
 * It is a UDP_DataAsset so it is identified and looked up by its stable DataTag through the data registry,
 * decoupling references from fragile asset paths.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSNARRATIVE_API UNarr_DialogueGraph : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The node id at which a run of this graph begins. Must name a node present in Nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	FGameplayTag StartNodeId;

	/** All nodes in this graph, keyed by their NodeId (see FindNode). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Graph")
	TArray<FNarr_DialogueNode> Nodes;

	//~ Begin UDP_DataAsset
	/** Groups all dialogue graphs under one asset-manager type bucket ("Narr_DialogueGraph"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** @return the node with NodeId, or null if absent. O(n) over Nodes (graphs are small; built once). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Graph")
	const FNarr_DialogueNode* FindNode(FGameplayTag NodeId) const;

	/** @return the designated start node, or null if StartNodeId is unset / dangling. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Graph")
	const FNarr_DialogueNode* GetStartNode() const;

	/** @return the number of nodes in this graph. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Graph")
	int32 GetNodeCount() const { return Nodes.Num(); }

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/**
	 * Validates structural integrity: a valid+present StartNodeId, unique node ids, and every edge's
	 * TargetNodeId either invalid (a deliberate terminator) or referencing an existing node.
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
