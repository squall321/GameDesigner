// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DPGameplayDebuggerCategory.h"

#if WITH_DP_GAMEPLAY_DEBUGGER

#include "GameplayDebuggerTypes.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPStateMachineComponent.h"
#include "Action/DPGameplayActionComponent.h"
#include "Pool/DPObjectPoolSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

FGameplayDebuggerCategory_DP::FGameplayDebuggerCategory_DP()
{
	// Replicate gathered data from authority to the viewing client so the overlay is
	// correct even when the debug actor only exists on the server.
	SetDataPackReplication<FRepData>(&DataPack);
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_DP::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_DP());
}

void FGameplayDebuggerCategory_DP::FRepData::Serialize(FArchive& Ar)
{
	Ar << ActorName;
	Ar << FsmState;
	Ar << GrantedActionTags;
	Ar << PoolSummary;
	Ar << BusSummary;
}

void FGameplayDebuggerCategory_DP::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	DataPack = FRepData();

	// ---- Per-actor components ----
	if (DebugActor)
	{
		DataPack.ActorName = DebugActor->GetName();

		if (const UDP_StateMachineComponent* Fsm = DebugActor->FindComponentByClass<UDP_StateMachineComponent>())
		{
			const FGameplayTag StateTag = Fsm->GetActiveStateTag();
			DataPack.FsmState = StateTag.IsValid() ? StateTag.ToString() : TEXT("<none>");
		}
		else
		{
			DataPack.FsmState = TEXT("<no FSM component>");
		}

		if (const UDP_GameplayActionComponent* Actions = DebugActor->FindComponentByClass<UDP_GameplayActionComponent>())
		{
			const FGameplayTagContainer& Granted = Actions->GetGrantedActionTags();
			DataPack.GrantedActionTags = Granted.IsEmpty() ? TEXT("<none>") : Granted.ToStringSimple();
		}
		else
		{
			DataPack.GrantedActionTags = TEXT("<no Action component>");
		}
	}
	else
	{
		DataPack.ActorName = TEXT("<no debug actor>");
	}

	// ---- Subsystem summaries (resolved from a world-context object) ----
	const UObject* WorldContext = DebugActor ? static_cast<UObject*>(DebugActor) : static_cast<UObject*>(OwnerPC);
	if (WorldContext)
	{
		if (UDP_ObjectPoolSubsystem* Pool = FDP_SubsystemStatics::GetWorldSubsystem<UDP_ObjectPoolSubsystem>(WorldContext))
		{
			DataPack.PoolSummary = Pool->GetDPDebugString();
		}
		else
		{
			DataPack.PoolSummary = TEXT("<pool subsystem unavailable>");
		}

		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(WorldContext))
		{
			DataPack.BusSummary = Bus->GetDPDebugString();
		}
		else
		{
			DataPack.BusSummary = TEXT("<bus subsystem unavailable>");
		}
	}
}

void FGameplayDebuggerCategory_DP::DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext)
{
	CanvasContext.Printf(TEXT("{yellow}DesignPatterns{white} debug actor: {green}%s"), *DataPack.ActorName);
	CanvasContext.Printf(TEXT("{yellow}FSM state:{white} %s"), *DataPack.FsmState);
	CanvasContext.Printf(TEXT("{yellow}Granted actions:{white} %s"), *DataPack.GrantedActionTags);
	CanvasContext.Printf(TEXT("{yellow}Pool:{white} %s"), *DataPack.PoolSummary);
	CanvasContext.Printf(TEXT("{yellow}Bus:{white} %s"), *DataPack.BusSummary);
}

#endif // WITH_DP_GAMEPLAY_DEBUGGER
