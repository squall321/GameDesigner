// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DP_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerCategory.h"

/**
 * Gameplay-debugger category for the DesignPatterns plugin.
 *
 * For the current debug actor this overlays:
 *  - the active FSM state tag (UDP_StateMachineComponent::GetActiveStateTag),
 *  - the granted-action tag list (UDP_GameplayActionComponent::GetGrantedActionTags, if present),
 *  - one-line summaries for the object-pool (world) and message-bus (game-instance) subsystems
 *    via their GetDPDebugString() native events.
 *
 * CollectData() runs on the authority/server side and packs a compact FReplicationData blob;
 * DrawData() runs on the local client and renders the replicated text. This split keeps the
 * category correct in networked PIE where the inspected actor lives on the server.
 */
class FGameplayDebuggerCategory_DP : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_DP();

	/** Factory used by IGameplayDebugger::RegisterCategory. */
	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

	//~ Begin FGameplayDebuggerCategory
	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;
	//~ End FGameplayDebuggerCategory

protected:
	/** Replicated text blob, gathered on the authority and drawn on the client. */
	struct FRepData
	{
		FString ActorName;
		FString FsmState;
		FString GrantedActionTags;
		FString PoolSummary;
		FString BusSummary;

		void Serialize(FArchive& Ar);
	};

	FRepData DataPack;
};

#endif // WITH_DP_GAMEPLAY_DEBUGGER
