// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Dialogue/Narr_DialogueGraph.h"
#include "Logic/Narr_Condition.h"
#include "Logic/Narr_Effect.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FName UNarr_DialogueGraph::GetDataAssetType_Implementation() const
{
	// Collapse every dialogue graph subclass into one asset-manager bucket so the registry / asset
	// manager can address all conversations as a single primary-asset type.
	static const FName Type(TEXT("Narr_DialogueGraph"));
	return Type;
}

const FNarr_DialogueNode* UNarr_DialogueGraph::FindNode(FGameplayTag NodeId) const
{
	if (!NodeId.IsValid())
	{
		return nullptr;
	}

	for (const FNarr_DialogueNode& Node : Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}
	return nullptr;
}

const FNarr_DialogueNode* UNarr_DialogueGraph::GetStartNode() const
{
	return FindNode(StartNodeId);
}

#if WITH_EDITOR
EDataValidationResult UNarr_DialogueGraph::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Start node must be set and present.
	if (!StartNodeId.IsValid())
	{
		Context.AddError(NSLOCTEXT("Narrative", "Narr_Graph_NoStart",
			"Dialogue graph has no StartNodeId set."));
		Result = EDataValidationResult::Invalid;
	}
	else if (!FindNode(StartNodeId))
	{
		Context.AddError(FText::Format(
			NSLOCTEXT("Narrative", "Narr_Graph_DanglingStart", "StartNodeId '{0}' does not match any node."),
			FText::FromString(StartNodeId.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	// Node ids must be valid and unique; every edge must terminate or point at an existing node.
	TSet<FGameplayTag> SeenIds;
	SeenIds.Reserve(Nodes.Num());
	for (const FNarr_DialogueNode& Node : Nodes)
	{
		if (!Node.NodeId.IsValid())
		{
			Context.AddError(NSLOCTEXT("Narrative", "Narr_Graph_NodeNoId", "A node has an invalid (empty) NodeId."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		SeenIds.Add(Node.NodeId, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Narrative", "Narr_Graph_DupId", "Duplicate NodeId '{0}'."),
				FText::FromString(Node.NodeId.ToString())));
			Result = EDataValidationResult::Invalid;
		}

		for (const FNarr_DialogueEdge& Edge : Node.Edges)
		{
			// An invalid TargetNodeId is a deliberate terminator; a valid one must resolve.
			if (Edge.TargetNodeId.IsValid() && !FindNode(Edge.TargetNodeId))
			{
				Context.AddError(FText::Format(
					NSLOCTEXT("Narrative", "Narr_Graph_DanglingEdge",
						"Node '{0}' has an edge to non-existent node '{1}'."),
					FText::FromString(Node.NodeId.ToString()),
					FText::FromString(Edge.TargetNodeId.ToString())));
				Result = EDataValidationResult::Invalid;
			}
		}

		// A choice node with no choice-bearing edges can never advance.
		if (Node.Kind == ENarr_NodeKind::Choice)
		{
			const bool bHasChoiceEdge = Node.Edges.ContainsByPredicate(
				[](const FNarr_DialogueEdge& E) { return E.IsChoice(); });
			if (!bHasChoiceEdge)
			{
				Context.AddWarning(FText::Format(
					NSLOCTEXT("Narrative", "Narr_Graph_ChoiceNoEdges",
						"Choice node '{0}' has no edges carrying a ChoiceId; it cannot present any options."),
					FText::FromString(Node.NodeId.ToString())));
			}
		}
	}

	return Result;
}
#endif // WITH_EDITOR
