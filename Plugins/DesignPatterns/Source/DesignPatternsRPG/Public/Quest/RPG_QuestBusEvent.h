// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPG_QuestBusEvent.generated.h"

/**
 * Flat, weak-ref-free message-bus payload for the RPG quest layer's observer-only events.
 *
 * Broadcast on the DP.Bus.RPG.Quest.* / DP.Bus.RPG.Journal.* channels (UI/audio/analytics may listen).
 * Holds only tags + an int, no UObject/weak references and no FInstancedStruct, so it is safe to queue
 * for deferred dispatch. These events NEVER drive authoritative flow — quest state is mutated only by the
 * server-authoritative tracker; the bus is for notification.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestBusEvent
{
	GENERATED_BODY()

	/** The quest this event relates to (matches the quest definition's DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest")
	FGameplayTag QuestTag;

	/** A stage tag, objective tag, or lore tag depending on the channel. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest")
	FGameplayTag NodeTag;

	/** Generic integer payload (new objective count, etc.). */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest")
	int32 IntValue = 0;

	FRPG_QuestBusEvent() = default;
	FRPG_QuestBusEvent(const FGameplayTag& InQuest, const FGameplayTag& InNode, int32 InValue = 0)
		: QuestTag(InQuest), NodeTag(InNode), IntValue(InValue) {}
};
