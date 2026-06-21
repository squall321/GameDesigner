// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Narr_DialogueHistoryComponent.generated.h"

/**
 * One recorded dialogue history entry: where the conversation was and what was chosen.
 *
 * Built locally by listening to the observer-only DP.Bus.Narrative.* events the runner already broadcasts.
 * Not replicated — each local machine records what it presented. Lets one-time nodes consult HasSeenNode and
 * lets UI show a backlog / "already asked" state.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_DialogueHistoryEntry
{
	GENERATED_BODY()

	/** The graph this entry belongs to (the graph's DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|History")
	FGameplayTag GraphTag;

	/** The node presented / committed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|History")
	FGameplayTag NodeId;

	/** For a line event the speaker; for a choice event the selected choice id. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|History")
	FGameplayTag SecondaryTag;

	/** Game-time seconds at which this entry was recorded. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|History")
	double Timestamp = 0.0;

	FNarr_DialogueHistoryEntry() = default;
	FNarr_DialogueHistoryEntry(const FGameplayTag& InGraph, const FGameplayTag& InNode, const FGameplayTag& InSecondary, double InTime)
		: GraphTag(InGraph), NodeId(InNode), SecondaryTag(InSecondary), Timestamp(InTime) {}
};

/** Fired locally whenever a new history entry is recorded. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNarr_OnHistoryRecorded, const FNarr_DialogueHistoryEntry&, Entry);

/**
 * Local, non-replicated dialogue history recorder.
 *
 * Subscribes (via UDP_MessageBusSubsystem::ListenNative) to the REAL observer-only narrative bus channels
 * (DP.Bus.Narrative.LineShown / ChoiceSelected / DialogueFinished) the runner already broadcasts with an
 * FNarr_DialogueBusEvent payload, and records each as a history entry. Provides HasSeenNode (so a runner /
 * the side-car extras can implement one-time nodes) and a per-graph history snapshot for UI backlogs.
 *
 * Transient + local-only: it records what THIS machine presented; nothing replicates. Add it to the same
 * actor that owns the dialogue runner.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent), Transient)
class DESIGNPATTERNSNARRATIVE_API UNarr_DialogueHistoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNarr_DialogueHistoryComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** @return every recorded entry for GraphTag (in record order). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|History")
	TArray<FNarr_DialogueHistoryEntry> GetHistory(FGameplayTag GraphTag) const;

	/** @return true if NodeId of GraphTag has been presented at least once this session/save. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|History")
	bool HasSeenNode(FGameplayTag GraphTag, FGameplayTag NodeId) const;

	/** @return true if a choice was committed on NodeId of GraphTag (any choice). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|History")
	bool HasChosenAtNode(FGameplayTag GraphTag, FGameplayTag NodeId) const;

	/** Clear all recorded history (e.g. on new game). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|History")
	void ClearHistory();

	/** Raised whenever a new entry is recorded. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|History")
	FNarr_OnHistoryRecorded OnHistoryRecorded;

	/**
	 * When true, choice-selected events are recorded (NodeId + chosen choice id). Lines are always recorded.
	 * Disable to keep only line history.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|History")
	bool bRecordChoices = true;

private:
	/** Recorded entries, in record order. */
	UPROPERTY(Transient)
	TArray<FNarr_DialogueHistoryEntry> Entries;

	/** Fast-lookup set of (GraphTag, NodeId) seen, for HasSeenNode without scanning Entries. */
	TSet<FString> SeenNodeKeys;

	/** Fast-lookup set of (GraphTag, NodeId) where a choice was committed. */
	TSet<FString> ChosenNodeKeys;

	/** Build the lookup key for (GraphTag, NodeId). */
	static FString MakeKey(const FGameplayTag& GraphTag, const FGameplayTag& NodeId);

	/** Common message handler: decode the payload, record, and broadcast. bIsChoice marks a choice event. */
	void HandleNarrativeBusEvent(const struct FDP_Message& Message, bool bIsChoice);
};
