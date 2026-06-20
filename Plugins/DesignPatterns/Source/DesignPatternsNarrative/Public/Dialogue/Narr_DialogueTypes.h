// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narr_DialogueTypes.generated.h"

/**
 * One presented line of dialogue handed to the local presenter seam.
 *
 * This is a COSMETIC presentation value: it is produced locally on each machine by the dialogue
 * runner from already-resolved graph data and is never replicated. It carries only what a view needs
 * to render one line — who is speaking, the localized text, and an optional auto-advance timeout.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueLine
{
	GENERATED_BODY()

	/** Speaker identity tag (Narr.Speaker.*). Empty for an unattributed / narrator line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FGameplayTag Speaker;

	/** Localized line text to display. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FText Text;

	/**
	 * Seconds after which the line auto-advances without player input. <= 0 means the line waits for an
	 * explicit AdvanceLine() from the view (the default, player-paced reading).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	float AutoAdvanceSeconds = 0.f;

	FNarr_DialogueLine() = default;

	FNarr_DialogueLine(const FGameplayTag& InSpeaker, const FText& InText, float InAutoAdvanceSeconds = 0.f)
		: Speaker(InSpeaker)
		, Text(InText)
		, AutoAdvanceSeconds(InAutoAdvanceSeconds)
	{
	}

	/** @return true if this line auto-advances rather than waiting for player input. */
	bool bAutoAdvances() const { return AutoAdvanceSeconds > 0.f; }
};

/**
 * One selectable choice handed to the local presenter seam as part of a choice set.
 *
 * COSMETIC presentation value (never replicated). The runner pre-evaluates each choice's gating
 * condition and bakes the result into bEnabled, so the view does not run the condition mini-language;
 * a disabled choice may still be shown greyed-out for legibility. ChoiceId identifies which outgoing
 * edge the runner takes when this choice is committed.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueChoice
{
	GENERATED_BODY()

	/** Localized choice text to display on the button. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FText Text;

	/** Stable id of this choice; the runner maps it back to an outgoing edge when committed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FGameplayTag ChoiceId;

	/**
	 * Whether this choice is currently selectable. The runner sets this from the choice's pre-evaluated
	 * gating condition; a false value means the view should disable (or hide) the option.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	bool bEnabled = true;

	FNarr_DialogueChoice() = default;

	FNarr_DialogueChoice(const FText& InText, const FGameplayTag& InChoiceId, bool bInEnabled = true)
		: Text(InText)
		, ChoiceId(InChoiceId)
		, bEnabled(bInEnabled)
	{
	}
};

/**
 * Why a dialogue run ended. Carried in the DP.Bus.Narrative.DialogueFinished observer payload and
 * passed to view code so it can distinguish a normal completion from a forced abort.
 */
UENUM(BlueprintType)
enum class ENarr_DialogueEndReason : uint8
{
	/** The graph reached a terminal node (no outgoing edges) and ended naturally. */
	Completed,

	/** Flow could not continue: a node had no satisfiable outgoing edge / a dangling reference. */
	DeadEnd,

	/** Explicitly stopped by gameplay code (StopDialogue) or by owner/world teardown. */
	Aborted
};

/**
 * Flat, weak-ref-free message-bus payload for narrative observer events.
 *
 * Broadcast on the DP.Bus.Narrative.* channels as OBSERVER-ONLY notifications (UI fx, audio, analytics
 * may listen). It holds no UObject/weak references and no FInstancedStruct, so it is safe to queue for
 * deferred dispatch. These events NEVER drive dialogue flow — flow is driven by the presenter seam.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueBusEvent
{
	GENERATED_BODY()

	/** The graph's DataTag (the graph's stable identity), so listeners can filter by conversation. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FGameplayTag GraphTag;

	/** The node id this event relates to (line/choice node), when applicable. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FGameplayTag NodeId;

	/** The speaker for a line event, or the selected/relevant choice id for a choice event. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	FGameplayTag SecondaryTag;

	/** Generic integer payload (choice count, end-reason cast to int, etc.). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Dialogue")
	int32 IntValue = 0;

	FNarr_DialogueBusEvent() = default;

	FNarr_DialogueBusEvent(const FGameplayTag& InGraphTag, const FGameplayTag& InNodeId,
		const FGameplayTag& InSecondaryTag, int32 InIntValue)
		: GraphTag(InGraphTag)
		, NodeId(InNodeId)
		, SecondaryTag(InSecondaryTag)
		, IntValue(InIntValue)
	{
	}
};
