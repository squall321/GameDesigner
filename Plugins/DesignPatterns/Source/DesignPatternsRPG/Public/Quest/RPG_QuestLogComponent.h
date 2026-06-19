// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RPG_QuestLogComponent.generated.h"

class URPG_QuestLogComponent;

/** Lifecycle state of a tracked quest. */
UENUM(BlueprintType)
enum class ERPG_QuestState : uint8
{
	/** Never started (default for any quest the log has not seen). */
	NotStarted,
	/** Accepted and in progress. */
	Active,
	/** All objectives satisfied. */
	Complete,
	/** Abandoned or failed. */
	Failed
};

/**
 * Saveable, restorable record of one quest's runtime progress.
 *
 * This is the unit persisted through the core Save system: a quest identity tag, its current
 * state, and per-objective counters. It is a plain USTRUCT so it serializes directly into a
 * UDP_SaveGame subclass's UPROPERTY array.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestProgress
{
	GENERATED_BODY()

	/** Identity of the quest (matches URPG_QuestDefinition::DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest")
	FGameplayTag QuestTag;

	/** Current lifecycle state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest")
	ERPG_QuestState State = ERPG_QuestState::NotStarted;

	/** Per-objective progress: objective tag -> accumulated count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest")
	TMap<FGameplayTag, int32> ObjectiveCounters;

	FRPG_QuestProgress() = default;
	explicit FRPG_QuestProgress(const FGameplayTag& InQuestTag)
		: QuestTag(InQuestTag), State(ERPG_QuestState::Active) {}
};

/** Broadcast when a quest's state changes (server and clients). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRPG_OnQuestStateChanged, URPG_QuestLogComponent*, QuestLog, FGameplayTag, QuestTag, ERPG_QuestState, NewState);
/** Broadcast when an objective counter advances (server side). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRPG_OnObjectiveProgress, URPG_QuestLogComponent*, QuestLog, FGameplayTag, QuestTag, FGameplayTag, ObjectiveTag, int32, NewCount);

/**
 * Server-authoritative quest tracker with save/restore through the core Save system.
 *
 * Holds an FRPG_QuestProgress per known quest. State transitions (StartQuest / advance /
 * Complete / Fail) are authority-only and early-return on clients. The set of currently
 * active quest tags is replicated (as an FGameplayTagContainer) so clients can drive quest
 * UI; full per-objective progress lives on the server and is persisted via the Save system.
 *
 * Persistence: ExportProgress() returns the full progress array for a UDP_SaveGame subclass
 * to store in OnPreSave; ImportProgress() restores it in OnPostLoad. Quest definitions are
 * resolved by tag through the core data registry when checking objective completion.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_QuestLogComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_QuestLogComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Begin tracking QuestTag in the Active state. AUTHORITY ONLY. Returns true if newly started. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	bool StartQuest(FGameplayTag QuestTag);

	/**
	 * Advance an objective counter by Delta. AUTHORITY ONLY. When all objectives of the quest
	 * reach their RequiredCount the quest auto-completes. Returns the new counter value.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	int32 AdvanceObjective(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 Delta = 1);

	/** Force a quest to Complete. AUTHORITY ONLY. Returns true if state changed. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	bool CompleteQuest(FGameplayTag QuestTag);

	/** Force a quest to Failed. AUTHORITY ONLY. Returns true if state changed. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	bool FailQuest(FGameplayTag QuestTag);

	/** Current state of a quest (NotStarted if untracked). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	ERPG_QuestState GetQuestState(FGameplayTag QuestTag) const;

	/** Accumulated counter for one objective (0 if untracked). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	int32 GetObjectiveCount(FGameplayTag QuestTag, FGameplayTag ObjectiveTag) const;

	/** Replicated set of currently-active quest tags (for UI). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	FGameplayTagContainer GetActiveQuests() const { return ActiveQuestTags; }

	/** Full progress snapshot for persistence (call on the server in OnPreSave). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	TArray<FRPG_QuestProgress> ExportProgress() const;

	/**
	 * Restore progress from a save (call on the server in OnPostLoad). AUTHORITY ONLY.
	 * Replaces all tracked progress and rebuilds the replicated active-quest set.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	void ImportProgress(const TArray<FRPG_QuestProgress>& InProgress);

	/** Designer hook fired after a quest's state changes; default broadcasts the delegate. */
	UFUNCTION(BlueprintNativeEvent, Category = "RPG|Quest")
	void NotifyQuestStateChanged(FGameplayTag QuestTag, ERPG_QuestState NewState);
	virtual void NotifyQuestStateChanged_Implementation(FGameplayTag QuestTag, ERPG_QuestState NewState);

	/** Broadcast on any quest state change (server + clients via OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Quest")
	FRPG_OnQuestStateChanged OnQuestStateChanged;

	/** Broadcast when an objective counter advances (server side). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Quest")
	FRPG_OnObjectiveProgress OnObjectiveProgress;

protected:
	/** OnRep for the replicated active-quest tag set: refresh quest UI on clients. */
	UFUNCTION()
	void OnRep_ActiveQuestTags();

private:
	/** Full per-quest progress (server-authoritative; not replicated wholesale). */
	UPROPERTY()
	TArray<FRPG_QuestProgress> Progress;

	/** Replicated set of active quest tags for client-side UI. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveQuestTags)
	FGameplayTagContainer ActiveQuestTags;

	/** Index of the progress entry for QuestTag, or INDEX_NONE. */
	int32 FindProgressIndex(const FGameplayTag& QuestTag) const;

	/** Recompute ActiveQuestTags from Progress (server side) and dirty for replication. */
	void RebuildActiveQuestTags();

	/** Set a quest's state, notify, and refresh the active-quest set. Caller must have authority. */
	void SetQuestStateInternal(FRPG_QuestProgress& Entry, ERPG_QuestState NewState);

	/** True if every objective of QuestTag has reached its required count. */
	bool AreAllObjectivesComplete(const FGameplayTag& QuestTag, const FRPG_QuestProgress& Entry) const;
};
