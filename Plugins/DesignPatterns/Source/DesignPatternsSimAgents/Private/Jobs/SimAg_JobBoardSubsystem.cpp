// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "Jobs/SimAg_JobBoardReplicator.h"
#include "Save/SimAg_SaveGame.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "EngineUtils.h"
#include "Engine/World.h"

//~ Lifecycle -----------------------------------------------------------------------------------

void USimAg_JobBoardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		RelevancyCap = FMath::Max(1, Settings->JobBoardRelevancyCap);
	}
	RegisteredServiceTag = SimAgNativeTags::Service_JobBoard;

	// Clients may already have a replicated board carrier; cache it if present.
	ResolveBoard();

	RegisterAsJobProvider();

	UE_LOG(LogDP, Log, TEXT("SimAg job board initialized (authority=%d, relevancyCap=%d)."),
		HasWorldAuthority() ? 1 : 0, RelevancyCap);
}

void USimAg_JobBoardSubsystem::Deinitialize()
{
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}
	BoardCarrier.Reset();
	Super::Deinitialize();
}

void USimAg_JobBoardSubsystem::RegisterAsJobProvider()
{
	if (!RegisteredServiceTag.IsValid())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and must not keep a dead world's subsystem alive.
		Locator->RegisterService(RegisteredServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

//~ Carrier resolution --------------------------------------------------------------------------

ASimAg_JobBoardReplicator* USimAg_JobBoardSubsystem::ResolveBoard() const
{
	if (ASimAg_JobBoardReplicator* Live = BoardCarrier.Get())
	{
		return Live;
	}

	// Discover an already-spawned/replicated carrier (clients, or a carrier restored from a save).
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASimAg_JobBoardReplicator> It(World); It; ++It)
		{
			if (ASimAg_JobBoardReplicator* Found = *It)
			{
				const_cast<USimAg_JobBoardSubsystem*>(this)->BoardCarrier = Found;
				return Found;
			}
		}
	}
	return nullptr;
}

ASimAg_JobBoardReplicator* USimAg_JobBoardSubsystem::GetOrSpawnBoard(bool bSpawnIfMissing)
{
	if (ASimAg_JobBoardReplicator* Live = ResolveBoard())
	{
		return Live;
	}
	if (!bSpawnIfMissing || !HasWorldAuthority())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // runtime state restored from saves, not a level actor

	ASimAg_JobBoardReplicator* Carrier = World->SpawnActor<ASimAg_JobBoardReplicator>(
		ASimAg_JobBoardReplicator::StaticClass(), FTransform::Identity, Params);
	if (!Carrier)
	{
		UE_LOG(LogDP, Error, TEXT("SimAg: failed to spawn job board carrier."));
		return nullptr;
	}
	BoardCarrier = Carrier;
	return Carrier;
}

//~ Relevance scan ------------------------------------------------------------------------------

const FSimAg_JobEntry* USimAg_JobBoardSubsystem::FindBestOpen(
	const ASimAg_JobBoardReplicator* Board, const FGameplayTag& JobKind, const FVector& AgentLocation) const
{
	if (!Board || !JobKind.IsValid())
	{
		return nullptr;
	}

	const FSimAg_JobEntry* Best = nullptr;
	float BestScore = -FLT_MAX;
	int32 Considered = 0;

	for (const FSimAg_JobEntry& Entry : Board->GetEntries())
	{
		if (Entry.State != ESimAg_JobState::Open)
		{
			continue;
		}
		// Tag-hierarchy match: a job of kind "X.Y" answers a query for "X".
		if (!Entry.JobKind.MatchesTag(JobKind))
		{
			continue;
		}
		if (++Considered > RelevancyCap)
		{
			break; // cap the scan cost on a crowded board
		}

		// Prefer higher Priority and shorter distance. Distance is normalized into priority units by a
		// gentle inverse so closer-but-lower-priority can still win; no hardcoded weight beyond the
		// designer-authored Priority field itself.
		const float Dist = static_cast<float>(FVector::Dist(AgentLocation, Entry.WorldLocation));
		const float Score = Entry.Priority - (Dist * 0.0001f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &Entry;
		}
	}
	return Best;
}

//~ ISimAg_JobProvider --------------------------------------------------------------------------

FGuid USimAg_JobBoardSubsystem::PostJob_Implementation(const FSimAg_JobRequest& Request)
{
	// AUTHORITY GUARD at top.
	if (!HasWorldAuthority())
	{
		return FGuid();
	}
	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ true);
	if (!Board)
	{
		return FGuid();
	}

	const FGuid NewId = Board->AddJob(Request);
	if (NewId.IsValid())
	{
		if (const FSimAg_JobEntry* Entry = Board->FindEntry(NewId))
		{
			EmitJobEvent(*Entry);
		}
	}
	return NewId;
}

FSimAg_JobHandle USimAg_JobBoardSubsystem::ClaimJob_Implementation(FGameplayTag JobKind, const FVector& AgentLocation)
{
	// The seam claim records an invalid claimant (anonymous). Identity-bearing claims route through
	// ClaimJobForAgent from the player-owned component.
	return ClaimJobForAgent(JobKind, AgentLocation, FSeam_EntityId::Invalid());
}

FSimAg_JobHandle USimAg_JobBoardSubsystem::ClaimJobForAgent(
	FGameplayTag JobKind, const FVector& AgentLocation, const FSeam_EntityId& AgentId)
{
	if (!HasWorldAuthority())
	{
		return FSimAg_JobHandle::Invalid();
	}
	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ false);
	if (!Board)
	{
		return FSimAg_JobHandle::Invalid();
	}

	const FSimAg_JobEntry* Best = FindBestOpen(Board, JobKind, AgentLocation);
	if (!Best)
	{
		return FSimAg_JobHandle::Invalid();
	}

	const FGuid TargetId = Best->JobId;
	if (!Board->ClaimJob(TargetId, AgentId))
	{
		return FSimAg_JobHandle::Invalid();
	}

	if (const FSimAg_JobEntry* Claimed = Board->FindEntry(TargetId))
	{
		EmitJobEvent(*Claimed);
		return Claimed->ToHandle();
	}
	return FSimAg_JobHandle::Invalid();
}

void USimAg_JobBoardSubsystem::CompleteJob_Implementation(const FGuid& JobId)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ false);
	if (!Board)
	{
		return;
	}
	if (Board->CompleteJob(JobId))
	{
		if (const FSimAg_JobEntry* Entry = Board->FindEntry(JobId))
		{
			EmitJobEvent(*Entry);
		}
	}
}

FSimAg_JobHandle USimAg_JobBoardSubsystem::QueryBestJobFor_Implementation(
	FGameplayTag JobKind, const FVector& AgentLocation) const
{
	// SIDE-EFFECT-FREE and client-safe: read the replicated board without mutating it.
	const ASimAg_JobBoardReplicator* Board = ResolveBoard();
	if (const FSimAg_JobEntry* Best = FindBestOpen(Board, JobKind, AgentLocation))
	{
		return Best->ToHandle();
	}
	return FSimAg_JobHandle::Invalid();
}

//~ Extra authority mutators --------------------------------------------------------------------

bool USimAg_JobBoardSubsystem::CancelJob(const FGuid& JobId)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ false);
	if (!Board)
	{
		return false;
	}
	if (Board->CancelJob(JobId))
	{
		if (const FSimAg_JobEntry* Entry = Board->FindEntry(JobId))
		{
			EmitJobEvent(*Entry);
		}
		return true;
	}
	return false;
}

int32 USimAg_JobBoardSubsystem::PruneCompletedJobs()
{
	if (!HasWorldAuthority())
	{
		return 0;
	}
	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ false);
	return Board ? Board->PruneTerminal() : 0;
}

int32 USimAg_JobBoardSubsystem::GetOpenJobCount() const
{
	const ASimAg_JobBoardReplicator* Board = ResolveBoard();
	return Board ? Board->CountOpen() : 0;
}

//~ Message bus ---------------------------------------------------------------------------------

void USimAg_JobBoardSubsystem::EmitJobEvent(const FSimAg_JobEntry& Entry) const
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FSimAg_JobEvent Event;
	Event.JobId = Entry.JobId;
	Event.JobKind = Entry.JobKind;
	Event.NewState = Entry.State;
	Event.Claimant = Entry.Claimant;

	FInstancedStruct Payload = FInstancedStruct::Make(Event);
	Bus->BroadcastPayload(SimAgNativeTags::Bus_JobChanged, Payload, const_cast<USimAg_JobBoardSubsystem*>(this));
}

//~ ISeam_Persistable ---------------------------------------------------------------------------

void USimAg_JobBoardSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimAg_JobBoardRecord Record;
	if (HasWorldAuthority())
	{
		if (const ASimAg_JobBoardReplicator* Board = ResolveBoard())
		{
			Record.Jobs.Reserve(Board->GetEntries().Num());
			for (const FSimAg_JobEntry& Entry : Board->GetEntries())
			{
				FSimAg_SavedJob Saved;
				Saved.JobId = Entry.JobId;
				Saved.JobKind = Entry.JobKind;
				Saved.RequiredSkill = Entry.RequiredSkill;
				Saved.WorldLocation = Entry.WorldLocation;
				Saved.Priority = Entry.Priority;
				Saved.Poster = Entry.Poster;
				Saved.Claimant = Entry.Claimant;
				Saved.State = Entry.State;
				Record.Jobs.Add(Saved);
			}
		}
	}
	Out = FInstancedStruct::Make(Record);
}

void USimAg_JobBoardSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client load must be a no-op (authoritative board arrives via replication).
	if (!HasWorldAuthority())
	{
		return;
	}
	const FSimAg_JobBoardRecord* Record = In.GetPtr<FSimAg_JobBoardRecord>();
	if (!Record)
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg job board RestoreState: record was not FSimAg_JobBoardRecord."));
		return;
	}

	ASimAg_JobBoardReplicator* Board = GetOrSpawnBoard(/*bSpawnIfMissing*/ true);
	if (!Board)
	{
		return;
	}

	// Re-post each saved job through the authority mutator, then re-apply its claimed/terminal state so
	// the restored board matches the snapshot exactly. Routing through AddJob keeps the same replication
	// path as live posting (we never write the fast array directly).
	for (const FSimAg_SavedJob& Saved : Record->Jobs)
	{
		FSimAg_JobRequest Request;
		Request.JobKind = Saved.JobKind;
		Request.RequiredSkill = Saved.RequiredSkill;
		Request.WorldLocation = Saved.WorldLocation;
		Request.Priority = Saved.Priority;
		Request.Poster = Saved.Poster;

		const FGuid NewId = Board->AddJob(Request);
		if (!NewId.IsValid())
		{
			continue;
		}
		switch (Saved.State)
		{
		case ESimAg_JobState::Claimed:
			Board->ClaimJob(NewId, Saved.Claimant);
			break;
		case ESimAg_JobState::Completed:
			Board->CompleteJob(NewId);
			break;
		case ESimAg_JobState::Cancelled:
			Board->CancelJob(NewId);
			break;
		default:
			break; // Open: already in the right state
		}
	}

	UE_LOG(LogDP, Log, TEXT("SimAg job board restored %d postings."), Record->Jobs.Num());
}

FGameplayTag USimAg_JobBoardSubsystem::GetPersistenceKind_Implementation() const
{
	return SimAgNativeTags::Persist_JobBoard;
}

//~ Debug ---------------------------------------------------------------------------------------

FString USimAg_JobBoardSubsystem::GetDPDebugString_Implementation() const
{
	const ASimAg_JobBoardReplicator* Board = ResolveBoard();
	const int32 Total = Board ? Board->GetEntries().Num() : 0;
	const int32 Open = Board ? Board->CountOpen() : 0;
	return FString::Printf(TEXT("SimAg JobBoard: %s | postings=%d open=%d"),
		HasWorldAuthority() ? TEXT("AUTHORITY") : TEXT("client"), Total, Open);
}
