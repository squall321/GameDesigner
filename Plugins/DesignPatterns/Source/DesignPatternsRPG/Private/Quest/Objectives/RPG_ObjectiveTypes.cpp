// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/Objectives/RPG_ObjectiveTypes.h"
#include "Quest/RPG_ObjectiveTrackerComponent.h"
#include "Quest/RPG_QuestBusEvent.h"
#include "Quest/RPG_QuestNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Inventory/Seam_ItemQuery.h"

#include "GameFramework/Actor.h"

namespace RPG_ObjectiveImpl
{
	/** Extract an FRPG_QuestBusEvent from a bus message, or return false if the payload is the wrong type. */
	static bool ReadQuestEvent(const FDP_Message& Message, FRPG_QuestBusEvent& Out)
	{
		if (Message.Payload.IsValid() && Message.Payload.GetScriptStruct() == FRPG_QuestBusEvent::StaticStruct())
		{
			Out = Message.Payload.Get<FRPG_QuestBusEvent>();
			return true;
		}
		return false;
	}

	/** Resolve the channel to listen on: the authored BusChannel if valid, otherwise the supplied default. */
	static FGameplayTag ResolveChannel(const FGameplayTag& Authored, const FGameplayTag& Default)
	{
		return Authored.IsValid() ? Authored : Default;
	}
}

// ===== URPG_Objective_KillTag ====================================================================

void URPG_Objective_KillTag::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[RPG] KillTag objective '%s': no message bus."), *ObjectiveTag.ToString());
		return;
	}

	const FGameplayTag Channel = RPG_ObjectiveImpl::ResolveChannel(BusChannel, RPG_QuestNativeTags::Bus_RPG_Objective_KillRoot);
	BusListener = Bus->ListenNative(Channel,
		[this](const FDP_Message& Message)
		{
			FRPG_QuestBusEvent Event;
			if (!RPG_ObjectiveImpl::ReadQuestEvent(Message, Event))
			{
				return;
			}
			// Filter by slain archetype (carried in NodeTag); empty TargetTag matches any kill.
			if (TargetTag.IsValid() && Event.NodeTag != TargetTag)
			{
				return;
			}
			ReportToTracker(FMath::Max(1, Event.IntValue));
		},
		this);
}

void URPG_Objective_KillTag::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_KillTag::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Kill: %s"), *TargetTag.ToString()));
}

// ===== URPG_Objective_CollectItem ================================================================

UObject* URPG_Objective_CollectItem::ResolveItemQuery() const
{
	const URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get();
	AActor* Owner = Tracker ? Tracker->GetOwner() : nullptr;
	if (!Owner)
	{
		return nullptr;
	}

	// Prefer a component implementing the seam, then the actor itself. Return the UObject so the
	// BlueprintNativeEvent Execute_ thunk can be invoked on it.
	if (UActorComponent* Comp = Owner->FindComponentByInterface(USeam_ItemQuery::StaticClass()))
	{
		return Comp;
	}
	if (Owner->Implements<USeam_ItemQuery>())
	{
		return Owner;
	}
	return nullptr;
}

int32 URPG_Objective_CollectItem::QueryCurrentProgress(const URPG_ObjectiveTrackerComponent* /*Tracker*/) const
{
	if (UObject* Query = ResolveItemQuery())
	{
		// BlueprintNativeEvent seam: call through the generated Execute_ thunk.
		return ISeam_ItemQuery::Execute_GetItemCount(Query, TargetTag);
	}
	return 0;
}

void URPG_Objective_CollectItem::Resync()
{
	if (URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get())
	{
		const int32 Count = QueryCurrentProgress(Tracker);
		// Absolute set so dropping items regresses progress.
		Tracker->SetObjectiveProgress(CachedQuestTag, ObjectiveTag, Count);
	}
}

void URPG_Objective_CollectItem::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	// Seed from the current inventory immediately.
	Resync();

	// Listen to a project-authored inventory-changed channel (BusChannel must be set for live updates).
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (Bus && BusChannel.IsValid())
	{
		BusListener = Bus->ListenNative(BusChannel,
			[this](const FDP_Message& /*Message*/) { Resync(); },
			this);
	}
	else
	{
		UE_LOG(LogDP, Verbose,
			TEXT("[RPG] CollectItem objective '%s': no BusChannel set; progress seeded once, no live updates."),
			*ObjectiveTag.ToString());
	}
}

void URPG_Objective_CollectItem::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_CollectItem::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Collect: %s"), *TargetTag.ToString()));
}

// ===== URPG_Objective_ReachLocation =============================================================

void URPG_Objective_ReachLocation::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}
	const FGameplayTag Channel = RPG_ObjectiveImpl::ResolveChannel(BusChannel, RPG_QuestNativeTags::Bus_RPG_Objective_ReachRoot);
	BusListener = Bus->ListenNative(Channel,
		[this](const FDP_Message& Message)
		{
			FRPG_QuestBusEvent Event;
			if (!RPG_ObjectiveImpl::ReadQuestEvent(Message, Event))
			{
				return;
			}
			if (TargetTag.IsValid() && Event.NodeTag != TargetTag)
			{
				return;
			}
			// Reaching a place is binary: report a large delta; the tracker clamps to RequiredCount.
			ReportToTracker(TNumericLimits<int32>::Max() / 2);
		},
		this);
}

void URPG_Objective_ReachLocation::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_ReachLocation::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Reach: %s"), *TargetTag.ToString()));
}

// ===== URPG_Objective_TalkToNpc =================================================================

void URPG_Objective_TalkToNpc::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}
	// Default to the narrative dialogue-finished channel (bus coupling, not a header include).
	static const FGameplayTag DialogueFinished =
		FGameplayTag::RequestGameplayTag(TEXT("DP.Bus.Narrative.DialogueFinished"), /*bErrorIfNotFound=*/false);
	const FGameplayTag Channel = RPG_ObjectiveImpl::ResolveChannel(BusChannel, DialogueFinished);
	if (!Channel.IsValid())
	{
		return;
	}
	BusListener = Bus->ListenNative(Channel,
		[this](const FDP_Message& Message)
		{
			// We can only see the project's rebroadcast FRPG_QuestBusEvent shape from RPG.
			FRPG_QuestBusEvent Event;
			if (!RPG_ObjectiveImpl::ReadQuestEvent(Message, Event))
			{
				return;
			}
			if (TargetTag.IsValid() && Event.NodeTag != TargetTag)
			{
				return;
			}
			ReportToTracker(TNumericLimits<int32>::Max() / 2);
		},
		this);
}

void URPG_Objective_TalkToNpc::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_TalkToNpc::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Talk to: %s"), *TargetTag.ToString()));
}

// ===== URPG_Objective_Escort ====================================================================

void URPG_Objective_Escort::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}
	const FGameplayTag Channel = RPG_ObjectiveImpl::ResolveChannel(BusChannel, RPG_QuestNativeTags::Bus_RPG_Objective_EscortRoot);
	BusListener = Bus->ListenNative(Channel,
		[this](const FDP_Message& Message)
		{
			FRPG_QuestBusEvent Event;
			if (!RPG_ObjectiveImpl::ReadQuestEvent(Message, Event))
			{
				return;
			}
			if (TargetTag.IsValid() && Event.NodeTag != TargetTag)
			{
				return;
			}
			if (Event.IntValue < 0)
			{
				// Escort target lost -> fail the quest.
				if (URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get())
				{
					Tracker->RequestFailQuest(CachedQuestTag);
				}
				return;
			}
			ReportToTracker(TNumericLimits<int32>::Max() / 2);
		},
		this);
}

void URPG_Objective_Escort::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_Escort::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Escort: %s"), *TargetTag.ToString()));
}

// ===== URPG_Objective_Defend ====================================================================

void URPG_Objective_Defend::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	Super::BeginTracking_Implementation(Tracker, QuestTag, InObjectiveTag);

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}
	const FGameplayTag Channel = RPG_ObjectiveImpl::ResolveChannel(BusChannel, RPG_QuestNativeTags::Bus_RPG_Objective_DefendRoot);
	BusListener = Bus->ListenNative(Channel,
		[this](const FDP_Message& Message)
		{
			FRPG_QuestBusEvent Event;
			if (!RPG_ObjectiveImpl::ReadQuestEvent(Message, Event))
			{
				return;
			}
			if (TargetTag.IsValid() && Event.NodeTag != TargetTag)
			{
				return;
			}
			if (Event.IntValue < 0)
			{
				if (URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get())
				{
					Tracker->RequestFailQuest(CachedQuestTag);
				}
				return;
			}
			ReportToTracker(TNumericLimits<int32>::Max() / 2);
		},
		this);
}

void URPG_Objective_Defend::EndTracking_Implementation()
{
	Super::EndTracking_Implementation();
}

FText URPG_Objective_Defend::DescribeObjective() const
{
	return FText::FromString(FString::Printf(TEXT("Defend: %s"), *TargetTag.ToString()));
}
