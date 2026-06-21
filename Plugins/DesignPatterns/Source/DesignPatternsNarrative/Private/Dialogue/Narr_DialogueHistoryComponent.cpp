// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Dialogue/Narr_DialogueHistoryComponent.h"
#include "Dialogue/Narr_DialogueTypes.h"          // FNarr_DialogueBusEvent
#include "DesignPatternsNarrativeModule.h"        // NarrativeNativeTags bus channels

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Engine/World.h"

UNarr_DialogueHistoryComponent::UNarr_DialogueHistoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // local-only recorder
}

void UNarr_DialogueHistoryComponent::BeginPlay()
{
	Super::BeginPlay();

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Narr] DialogueHistory: no message bus; history disabled."));
		return;
	}

	// Lines are always recorded.
	Bus->ListenNative(NarrativeNativeTags::Bus_Narrative_LineShown,
		[this](const FDP_Message& Message) { HandleNarrativeBusEvent(Message, /*bIsChoice=*/false); },
		this);

	// Choices (committed) recorded when enabled.
	Bus->ListenNative(NarrativeNativeTags::Bus_Narrative_ChoiceSelected,
		[this](const FDP_Message& Message)
		{
			if (bRecordChoices)
			{
				HandleNarrativeBusEvent(Message, /*bIsChoice=*/true);
			}
		},
		this);
}

void UNarr_DialogueHistoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	Super::EndPlay(EndPlayReason);
}

FString UNarr_DialogueHistoryComponent::MakeKey(const FGameplayTag& GraphTag, const FGameplayTag& NodeId)
{
	return FString::Printf(TEXT("%s|%s"), *GraphTag.ToString(), *NodeId.ToString());
}

void UNarr_DialogueHistoryComponent::HandleNarrativeBusEvent(const FDP_Message& Message, bool bIsChoice)
{
	// Decode the runner's flat payload. Ignore any message whose payload is not the dialogue event type.
	if (!Message.Payload.IsValid() || Message.Payload.GetScriptStruct() != FNarr_DialogueBusEvent::StaticStruct())
	{
		return;
	}
	const FNarr_DialogueBusEvent& Event = Message.Payload.Get<FNarr_DialogueBusEvent>();
	if (!Event.GraphTag.IsValid() || !Event.NodeId.IsValid())
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FNarr_DialogueHistoryEntry Entry(Event.GraphTag, Event.NodeId, Event.SecondaryTag, Now);
	Entries.Add(Entry);

	const FString Key = MakeKey(Event.GraphTag, Event.NodeId);
	SeenNodeKeys.Add(Key);
	if (bIsChoice)
	{
		ChosenNodeKeys.Add(Key);
	}

	OnHistoryRecorded.Broadcast(Entry);
}

TArray<FNarr_DialogueHistoryEntry> UNarr_DialogueHistoryComponent::GetHistory(FGameplayTag GraphTag) const
{
	TArray<FNarr_DialogueHistoryEntry> Out;
	for (const FNarr_DialogueHistoryEntry& Entry : Entries)
	{
		if (!GraphTag.IsValid() || Entry.GraphTag == GraphTag)
		{
			Out.Add(Entry);
		}
	}
	return Out;
}

bool UNarr_DialogueHistoryComponent::HasSeenNode(FGameplayTag GraphTag, FGameplayTag NodeId) const
{
	return SeenNodeKeys.Contains(MakeKey(GraphTag, NodeId));
}

bool UNarr_DialogueHistoryComponent::HasChosenAtNode(FGameplayTag GraphTag, FGameplayTag NodeId) const
{
	return ChosenNodeKeys.Contains(MakeKey(GraphTag, NodeId));
}

void UNarr_DialogueHistoryComponent::ClearHistory()
{
	Entries.Reset();
	SeenNodeKeys.Reset();
	ChosenNodeKeys.Reset();
}
