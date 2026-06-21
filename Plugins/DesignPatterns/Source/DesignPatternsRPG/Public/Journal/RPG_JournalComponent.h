// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Journal/RPG_LoreDataAsset.h"
#include "Quest/RPG_QuestLogComponent.h"   // ERPG_QuestState
#include "RPG_JournalComponent.generated.h"

class URPG_QuestLogComponent;
class URPG_LoreDataAsset;

/** Fired locally when the journal's aggregated view changes (a quest state change, a lore unlock). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPG_OnJournalChanged);

/**
 * Local aggregation surface over the real quest log + the lore catalog.
 *
 * Presentation/aggregation only: it reads the (already-replicated) quest log and the (replicated/saved)
 * world-hub lore flags and exposes a flattened journal view to UI. The ONE authoritative mutation it
 * performs is UnlockLore, which writes a Global-scope hub flag (authority-only, in .cpp) — the canonical
 * unlock state lives in the hub, replicating + saving through the hub's single path.
 *
 * The journal owns no replicated state of its own; on clients it observes quest-state changes via the log's
 * delegates and lore unlocks via the hub's OnValueChanged, re-raising OnJournalChanged.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_JournalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_JournalComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** The lore bundles this journal aggregates. Authored on the component or resolved by tag at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Journal")
	TArray<TObjectPtr<URPG_LoreDataAsset>> LoreBundles;

	/**
	 * Unlock a lore entry. AUTHORITY ONLY: writes a Global-scope hub flag keyed by LoreTag (under the lore
	 * root). Returns true if it was newly unlocked. A no-op on clients (the flag replicates from the server).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Journal")
	bool UnlockLore(FGameplayTag LoreTag);

	/** @return true if LoreTag is unlocked (its hub flag is set). Safe on clients. */
	UFUNCTION(BlueprintPure, Category = "RPG|Journal")
	bool IsLoreUnlocked(FGameplayTag LoreTag) const;

	/** @return all currently-unlocked lore entries across the bundles. */
	UFUNCTION(BlueprintPure, Category = "RPG|Journal")
	TArray<FRPG_LoreEntry> GetUnlockedLore() const;

	/** @return quest tags currently in the given state (reads the quest log). */
	UFUNCTION(BlueprintPure, Category = "RPG|Journal")
	TArray<FGameplayTag> GetQuestsByState(ERPG_QuestState State) const;

	/** Raised locally whenever the aggregated journal view changes. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Journal")
	FRPG_OnJournalChanged OnJournalChanged;

private:
	/** Resolve the quest log off the owner. */
	URPG_QuestLogComponent* GetQuestLog() const;

	/** True on server / standalone / listen host. */
	bool HasAuthoritySafe() const;

	/** Compute the hub flag key for a lore tag (the lore tag itself, under the lore-root namespace). */
	FGameplayTag GetLoreFlagKey(const FGameplayTag& LoreTag) const;

	//~ Observer handlers ------------------------------------------------------------------------
	UFUNCTION()
	void HandleQuestStateChanged(URPG_QuestLogComponent* QuestLog, FGameplayTag QuestTag, ERPG_QuestState NewState);

	/**
	 * Listen for lore-unlock notifications on the message bus (the quest layer broadcasts
	 * DP.Bus.RPG.Journal.LoreUnlocked when a flag is set). Refreshes the journal on clients without pulling
	 * any World type into this public header. Bound via UDP_MessageBusSubsystem::ListenNative in .cpp.
	 */
	void HandleLoreUnlockedBus(const struct FDP_Message& Message);
};
