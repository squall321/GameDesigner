// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/SimAg_SaveGame.h"
#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "Jobs/SimAg_JobReservationSubsystem.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Memory/SimAg_MemoryComponent.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Persist/Seam_Persistable.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

bool USimAg_SaveGame::WorldHasAuthority(const UWorld* World)
{
	return World && World->GetNetMode() != NM_Client;
}

bool USimAg_SaveGame::CaptureFrom(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg save CaptureFrom: null world."));
		return false;
	}
	// Gather/capture MUST run on the game thread (the save subsystem guarantees OnPreSave runs there).
	check(IsInGameThread());

	// Only authority holds the complete state; a client would capture partial, possibly-stale data.
	if (!WorldHasAuthority(World))
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg save CaptureFrom: refused on non-authority."));
		return false;
	}

	ParticipantRecords.Reset();
	ParticipantKinds.Reset();
	JobBoard = FSimAg_JobBoardRecord();

	// 1) Capture the job board subsystem (a single world-scoped participant).
	if (USimAg_JobBoardSubsystem* Board = World->GetSubsystem<USimAg_JobBoardSubsystem>())
	{
		FInstancedStruct Record;
		ISeam_Persistable::Execute_CaptureState(Board, Record);
		if (Record.IsValid())
		{
			// Mirror the job board into the convenience-typed field too, for direct BP access.
			if (const FSimAg_JobBoardRecord* AsBoard = Record.GetPtr<FSimAg_JobBoardRecord>())
			{
				JobBoard = *AsBoard;
			}
			ParticipantRecords.Add(Record);
			ParticipantKinds.Add(ISeam_Persistable::Execute_GetPersistenceKind(Board));
		}
	}

	// 1b) Capture the reservation router (a single world-scoped participant). ADDITIVE: the gather here is
	// not interface-generic, so new world participants are captured by extending this body.
	if (USimAg_JobReservationSubsystem* Reservation = World->GetSubsystem<USimAg_JobReservationSubsystem>())
	{
		FInstancedStruct Record;
		ISeam_Persistable::Execute_CaptureState(Reservation, Record);
		if (Record.IsValid())
		{
			ParticipantRecords.Add(Record);
			ParticipantKinds.Add(ISeam_Persistable::Execute_GetPersistenceKind(Reservation));
		}
	}

	// 2) Capture every agent component in the world (one participant per agent), plus its memory component
	// (a second per-agent participant routed by the new Persist_Memory kind).
	int32 AgentCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (USimAg_AgentComponent* Agent = Actor->FindComponentByClass<USimAg_AgentComponent>())
		{
			FInstancedStruct Record;
			ISeam_Persistable::Execute_CaptureState(Agent, Record);
			if (Record.IsValid())
			{
				ParticipantRecords.Add(Record);
				ParticipantKinds.Add(ISeam_Persistable::Execute_GetPersistenceKind(Agent));
				++AgentCount;
			}
		}
		if (USimAg_MemoryComponent* Memory = Actor->FindComponentByClass<USimAg_MemoryComponent>())
		{
			FInstancedStruct Record;
			ISeam_Persistable::Execute_CaptureState(Memory, Record);
			if (Record.IsValid())
			{
				ParticipantRecords.Add(Record);
				ParticipantKinds.Add(ISeam_Persistable::Execute_GetPersistenceKind(Memory));
			}
		}
	}

	UE_LOG(LogDP, Log, TEXT("SimAg save captured %d job postings and %d agents (%d records)."),
		JobBoard.Jobs.Num(), AgentCount, ParticipantRecords.Num());
	return true;
}

int32 USimAg_SaveGame::RestoreInto(UWorld* World) const
{
	if (!World)
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg save RestoreInto: null world."));
		return 0;
	}
	check(IsInGameThread());

	if (!WorldHasAuthority(World))
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg save RestoreInto: refused on non-authority."));
		return 0;
	}
	if (ParticipantRecords.Num() != ParticipantKinds.Num())
	{
		UE_LOG(LogDP, Error, TEXT("SimAg save RestoreInto: record/kind arrays out of sync (%d vs %d)."),
			ParticipantRecords.Num(), ParticipantKinds.Num());
		return 0;
	}

	// Build a fast lookup of agents by their stable id so agent records route to the right pawn, plus a
	// parallel map of their memory components (keyed by the same agent id) for memory-record routing.
	TMap<FGuid, USimAg_AgentComponent*> AgentsById;
	TMap<FGuid, USimAg_MemoryComponent*> MemoryByAgentId;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (USimAg_AgentComponent* Agent = Actor->FindComponentByClass<USimAg_AgentComponent>())
		{
			AgentsById.Add(Agent->GetAgentId().Value, Agent);
			if (USimAg_MemoryComponent* Memory = Actor->FindComponentByClass<USimAg_MemoryComponent>())
			{
				MemoryByAgentId.Add(Agent->GetAgentId().Value, Memory);
			}
		}
	}

	USimAg_JobBoardSubsystem* Board = World->GetSubsystem<USimAg_JobBoardSubsystem>();
	USimAg_JobReservationSubsystem* Reservation = World->GetSubsystem<USimAg_JobReservationSubsystem>();

	int32 Applied = 0;
	for (int32 Index = 0; Index < ParticipantRecords.Num(); ++Index)
	{
		const FInstancedStruct& Record = ParticipantRecords[Index];
		const FGameplayTag& Kind = ParticipantKinds[Index];
		if (!Record.IsValid())
		{
			continue;
		}

		if (Kind.MatchesTag(SimAgNativeTags::Persist_JobBoard))
		{
			if (Board)
			{
				ISeam_Persistable::Execute_RestoreState(Board, Record);
				++Applied;
			}
		}
		else if (Kind.MatchesTag(SimAgNativeTags::Persist_Reservation))
		{
			if (Reservation)
			{
				ISeam_Persistable::Execute_RestoreState(Reservation, Record);
				++Applied;
			}
		}
		else if (Kind.MatchesTag(SimAgNativeTags::Persist_Memory))
		{
			// Route by the record's agent id to the matching memory component.
			if (const FSimAg_MemoryRecord* MemRec = Record.GetPtr<FSimAg_MemoryRecord>())
			{
				if (USimAg_MemoryComponent** Found = MemoryByAgentId.Find(MemRec->AgentId.Value))
				{
					ISeam_Persistable::Execute_RestoreState(*Found, Record);
					++Applied;
				}
				else
				{
					UE_LOG(LogDP, Verbose, TEXT("SimAg save RestoreInto: no live memory for agent %s."),
						*MemRec->AgentId.ToString());
				}
			}
		}
		else if (Kind.MatchesTag(SimAgNativeTags::Persist_Agent))
		{
			// Route by the record's agent id (each agent's RestoreState re-checks authority itself).
			if (const FSimAg_AgentRecord* AgentRec = Record.GetPtr<FSimAg_AgentRecord>())
			{
				if (USimAg_AgentComponent** Found = AgentsById.Find(AgentRec->AgentId.Value))
				{
					ISeam_Persistable::Execute_RestoreState(*Found, Record);
					++Applied;
				}
				else
				{
					UE_LOG(LogDP, Verbose, TEXT("SimAg save RestoreInto: no live agent for id %s."),
						*AgentRec->AgentId.ToString());
				}
			}
		}
	}

	UE_LOG(LogDP, Log, TEXT("SimAg save restored %d/%d participant records."), Applied, ParticipantRecords.Num());
	return Applied;
}

void USimAg_SaveGame::OnPreSave_Implementation()
{
	Super::OnPreSave_Implementation();
	// Capture is driven explicitly via CaptureFrom (it needs the world). Nothing extra here.
}

void USimAg_SaveGame::OnPostLoad_Implementation()
{
	Super::OnPostLoad_Implementation();
	// Scatter is driven explicitly via RestoreInto once the target world is available.
}
