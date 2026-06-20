// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Logic/Narr_Effect.h"
#include "Dialogue/Narr_DialogueTypes.h"
#include "DesignPatternsNarrativeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// NOTE: the UNarr_Effect base and the write leaves (UNarr_Effect_SetFlag / _AddCounter) are implemented
// in the story-director area (Narr_StoryConditionTypes.cpp). This translation unit owns ONLY the net-new
// observer-only bus-broadcast leaf so there is exactly one definition of each shared symbol.

void UNarr_Effect_BroadcastBusEvent::Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	// The source object (story director / runner) is our world context for reaching the message bus.
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext)
	{
		UE_LOG(LogDP, Warning, TEXT("UNarr_Effect_BroadcastBusEvent: invalid source; skipping."));
		return;
	}

	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(WorldContext);
	if (!Bus)
	{
		UE_LOG(LogDP, Warning, TEXT("UNarr_Effect_BroadcastBusEvent: message bus unavailable; skipping."));
		return;
	}

	// Default to the narrative story-event channel when no explicit channel is authored.
	const FGameplayTag ResolvedChannel = Channel.IsValid() ? Channel : NarrativeNativeTags::Bus_Narrative_StoryEvent;

	// Flat, weak-ref-free payload (safe to queue for deferred dispatch).
	FNarr_DialogueBusEvent Event;
	Event.SecondaryTag = EventId;
	Event.IntValue = IntValue;

	FInstancedStruct Payload;
	Payload.InitializeAs<FNarr_DialogueBusEvent>(Event);
	Bus->BroadcastPayload(ResolvedChannel, Payload, WorldContext);

	UE_LOG(LogDP, Verbose, TEXT("UNarr_Effect_BroadcastBusEvent: channel %s event %s int %d"),
		*ResolvedChannel.ToString(), *EventId.ToString(), IntValue);
}

FString UNarr_Effect_BroadcastBusEvent::DescribeEffect() const
{
	return FString::Printf(TEXT("BroadcastBusEvent(%s : %s)"),
		Channel.IsValid() ? *Channel.ToString() : TEXT("DP.Bus.Narrative.StoryEvent"),
		*EventId.ToString());
}
