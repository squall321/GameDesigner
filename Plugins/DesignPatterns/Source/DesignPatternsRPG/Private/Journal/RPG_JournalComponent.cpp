// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Journal/RPG_JournalComponent.h"
#include "Journal/RPG_LoreDataAsset.h"
#include "Quest/RPG_QuestLogComponent.h"
#include "Quest/RPG_QuestNativeTags.h"
#include "Quest/RPG_QuestBusEvent.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

// World hub (PRIVATE dependency — used only here in .cpp).
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"

#include "GameFramework/Actor.h"

URPG_JournalComponent::URPG_JournalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // local aggregation surface; canonical state is the hub + quest log
}

void URPG_JournalComponent::BeginPlay()
{
	Super::BeginPlay();

	if (URPG_QuestLogComponent* Log = GetQuestLog())
	{
		Log->OnQuestStateChanged.AddDynamic(this, &URPG_JournalComponent::HandleQuestStateChanged);
	}

	// Listen for lore-unlock notifications (no World type in the header; bus coupling only).
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->ListenNative(RPG_QuestNativeTags::Bus_RPG_Journal_LoreUnlocked,
			[this](const FDP_Message& Message) { HandleLoreUnlockedBus(Message); },
			this);
	}
}

void URPG_JournalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (URPG_QuestLogComponent* Log = GetQuestLog())
	{
		Log->OnQuestStateChanged.RemoveDynamic(this, &URPG_JournalComponent::HandleQuestStateChanged);
	}
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	Super::EndPlay(EndPlayReason);
}

URPG_QuestLogComponent* URPG_JournalComponent::GetQuestLog() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<URPG_QuestLogComponent>() : nullptr;
}

bool URPG_JournalComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

FGameplayTag URPG_JournalComponent::GetLoreFlagKey(const FGameplayTag& LoreTag) const
{
	// The lore tag IS the flag key; lore tags are authored under RPG.Lore.* and are unique + stable, so they
	// serve directly as hub keys. (Projects wanting a separate namespace can remap here.)
	return LoreTag;
}

bool URPG_JournalComponent::UnlockLore(FGameplayTag LoreTag)
{
	if (!HasAuthoritySafe() || !LoreTag.IsValid())
	{
		return false;
	}

	UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (!Hub)
	{
		UE_LOG(LogDP, Warning, TEXT("[RPG] UnlockLore: no world hub available."));
		return false;
	}

	const FGameplayTag Key = GetLoreFlagKey(LoreTag);
	if (Hub->QueryFlag(Key, FWorldHub_Scope::Global(), false))
	{
		return false; // already unlocked
	}

	Hub->SetFlag(Key, true, FWorldHub_Scope::Global());

	// Notify observers (UI / this component on clients) via the bus.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		const FRPG_QuestBusEvent Payload(FGameplayTag(), LoreTag, 0);
		Bus->BroadcastPayload(RPG_QuestNativeTags::Bus_RPG_Journal_LoreUnlocked, FInstancedStruct::Make(Payload), this);
	}

	UE_LOG(LogDP, Log, TEXT("[RPG] Lore '%s' unlocked."), *LoreTag.ToString());
	OnJournalChanged.Broadcast();
	return true;
}

bool URPG_JournalComponent::IsLoreUnlocked(FGameplayTag LoreTag) const
{
	if (!LoreTag.IsValid())
	{
		return false;
	}
	if (UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		return Hub->QueryFlag(GetLoreFlagKey(LoreTag), FWorldHub_Scope::Global(), false);
	}
	return false;
}

TArray<FRPG_LoreEntry> URPG_JournalComponent::GetUnlockedLore() const
{
	TArray<FRPG_LoreEntry> Out;
	for (const TObjectPtr<URPG_LoreDataAsset>& Bundle : LoreBundles)
	{
		if (!Bundle)
		{
			continue;
		}
		for (const FRPG_LoreEntry& Entry : Bundle->Entries)
		{
			if (IsLoreUnlocked(Entry.LoreTag))
			{
				Out.Add(Entry);
			}
		}
	}
	Out.Sort([](const FRPG_LoreEntry& A, const FRPG_LoreEntry& B) { return A.SortOrder < B.SortOrder; });
	return Out;
}

TArray<FGameplayTag> URPG_JournalComponent::GetQuestsByState(ERPG_QuestState State) const
{
	TArray<FGameplayTag> Out;
	const URPG_QuestLogComponent* Log = GetQuestLog();
	if (!Log)
	{
		return Out;
	}
	// The log replicates the active set; for Active we use it directly. For other states we query the log's
	// per-quest state across the active + recently-changed set the UI knows about.
	if (State == ERPG_QuestState::Active)
	{
		const FGameplayTagContainer Active = Log->GetActiveQuests();
		for (const FGameplayTag& Tag : Active)
		{
			Out.Add(Tag);
		}
	}
	else
	{
		// Non-active states are not enumerated by the replicated set; the UI typically tracks completed/failed
		// quests it has seen. We expose a per-tag check via the log for any tag the caller already knows.
		const FGameplayTagContainer Active = Log->GetActiveQuests();
		for (const FGameplayTag& Tag : Active)
		{
			if (Log->GetQuestState(Tag) == State)
			{
				Out.Add(Tag);
			}
		}
	}
	return Out;
}

void URPG_JournalComponent::HandleQuestStateChanged(URPG_QuestLogComponent* /*QuestLog*/, FGameplayTag /*QuestTag*/, ERPG_QuestState /*NewState*/)
{
	OnJournalChanged.Broadcast();
}

void URPG_JournalComponent::HandleLoreUnlockedBus(const FDP_Message& /*Message*/)
{
	OnJournalChanged.Broadcast();
}
