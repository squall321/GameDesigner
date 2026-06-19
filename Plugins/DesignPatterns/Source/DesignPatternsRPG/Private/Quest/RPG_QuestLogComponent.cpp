// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestLogComponent.h"
#include "Quest/RPG_QuestDefinition.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

URPG_QuestLogComponent::URPG_QuestLogComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void URPG_QuestLogComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_QuestLogComponent, ActiveQuestTags);
}

int32 URPG_QuestLogComponent::FindProgressIndex(const FGameplayTag& QuestTag) const
{
	for (int32 Index = 0; Index < Progress.Num(); ++Index)
	{
		if (Progress[Index].QuestTag == QuestTag)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void URPG_QuestLogComponent::RebuildActiveQuestTags()
{
	FGameplayTagContainer NewActive;
	for (const FRPG_QuestProgress& Entry : Progress)
	{
		if (Entry.State == ERPG_QuestState::Active)
		{
			NewActive.AddTag(Entry.QuestTag);
		}
	}
	ActiveQuestTags = NewActive;
}

void URPG_QuestLogComponent::SetQuestStateInternal(FRPG_QuestProgress& Entry, ERPG_QuestState NewState)
{
	if (Entry.State == NewState)
	{
		return;
	}
	Entry.State = NewState;
	RebuildActiveQuestTags();
	NotifyQuestStateChanged(Entry.QuestTag, NewState);
}

bool URPG_QuestLogComponent::StartQuest(FGameplayTag QuestTag)
{
	// AUTHORITY GUARD: quest state is server-authoritative.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (!QuestTag.IsValid())
	{
		return false;
	}

	int32 Index = FindProgressIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		Progress.Add(FRPG_QuestProgress(QuestTag));
		RebuildActiveQuestTags();
		NotifyQuestStateChanged(QuestTag, ERPG_QuestState::Active);
		UE_LOG(LogDPData, Verbose, TEXT("[RPG_Quest] Started %s"), *QuestTag.ToString());
		return true;
	}

	FRPG_QuestProgress& Entry = Progress[Index];
	if (Entry.State == ERPG_QuestState::NotStarted || Entry.State == ERPG_QuestState::Failed)
	{
		SetQuestStateInternal(Entry, ERPG_QuestState::Active);
		return true;
	}
	return false;
}

bool URPG_QuestLogComponent::AreAllObjectivesComplete(const FGameplayTag& QuestTag, const FRPG_QuestProgress& Entry) const
{
	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	if (!Registry)
	{
		return false;
	}
	const URPG_QuestDefinition* Def = Registry->Find<URPG_QuestDefinition>(QuestTag);
	if (!Def || Def->Objectives.Num() == 0)
	{
		return false;
	}

	for (const FRPG_QuestObjective& Objective : Def->Objectives)
	{
		const int32* Counter = Entry.ObjectiveCounters.Find(Objective.ObjectiveTag);
		const int32 Current = Counter ? *Counter : 0;
		if (Current < Objective.RequiredCount)
		{
			return false;
		}
	}
	return true;
}

int32 URPG_QuestLogComponent::AdvanceObjective(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 Delta)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	if (!QuestTag.IsValid() || !ObjectiveTag.IsValid() || Delta == 0)
	{
		return 0;
	}

	const int32 Index = FindProgressIndex(QuestTag);
	if (Index == INDEX_NONE || Progress[Index].State != ERPG_QuestState::Active)
	{
		return 0;
	}

	FRPG_QuestProgress& Entry = Progress[Index];
	int32& Counter = Entry.ObjectiveCounters.FindOrAdd(ObjectiveTag);
	Counter = FMath::Max(0, Counter + Delta);

	OnObjectiveProgress.Broadcast(this, QuestTag, ObjectiveTag, Counter);

	if (AreAllObjectivesComplete(QuestTag, Entry))
	{
		SetQuestStateInternal(Entry, ERPG_QuestState::Complete);
	}
	return Counter;
}

bool URPG_QuestLogComponent::CompleteQuest(FGameplayTag QuestTag)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const int32 Index = FindProgressIndex(QuestTag);
	if (Index == INDEX_NONE || Progress[Index].State == ERPG_QuestState::Complete)
	{
		return false;
	}
	SetQuestStateInternal(Progress[Index], ERPG_QuestState::Complete);
	return true;
}

bool URPG_QuestLogComponent::FailQuest(FGameplayTag QuestTag)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const int32 Index = FindProgressIndex(QuestTag);
	if (Index == INDEX_NONE || Progress[Index].State == ERPG_QuestState::Failed)
	{
		return false;
	}
	SetQuestStateInternal(Progress[Index], ERPG_QuestState::Failed);
	return true;
}

ERPG_QuestState URPG_QuestLogComponent::GetQuestState(FGameplayTag QuestTag) const
{
	const int32 Index = FindProgressIndex(QuestTag);
	return Index != INDEX_NONE ? Progress[Index].State : ERPG_QuestState::NotStarted;
}

int32 URPG_QuestLogComponent::GetObjectiveCount(FGameplayTag QuestTag, FGameplayTag ObjectiveTag) const
{
	const int32 Index = FindProgressIndex(QuestTag);
	if (Index == INDEX_NONE)
	{
		return 0;
	}
	const int32* Counter = Progress[Index].ObjectiveCounters.Find(ObjectiveTag);
	return Counter ? *Counter : 0;
}

TArray<FRPG_QuestProgress> URPG_QuestLogComponent::ExportProgress() const
{
	return Progress;
}

void URPG_QuestLogComponent::ImportProgress(const TArray<FRPG_QuestProgress>& InProgress)
{
	// AUTHORITY GUARD: restoring rewrites authoritative + replicated state.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Progress = InProgress;
	RebuildActiveQuestTags();

	// Notify for every tracked quest so listeners rebuild their UI after a load.
	for (const FRPG_QuestProgress& Entry : Progress)
	{
		NotifyQuestStateChanged(Entry.QuestTag, Entry.State);
	}
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Quest] Imported %d quests"), Progress.Num());
}

void URPG_QuestLogComponent::OnRep_ActiveQuestTags()
{
	// Surface a generic refresh on clients: re-broadcast active quests as state changes.
	for (const FGameplayTag& Tag : ActiveQuestTags.GetGameplayTagArray())
	{
		NotifyQuestStateChanged(Tag, ERPG_QuestState::Active);
	}
}

void URPG_QuestLogComponent::NotifyQuestStateChanged_Implementation(FGameplayTag QuestTag, ERPG_QuestState NewState)
{
	OnQuestStateChanged.Broadcast(this, QuestTag, NewState);
}
