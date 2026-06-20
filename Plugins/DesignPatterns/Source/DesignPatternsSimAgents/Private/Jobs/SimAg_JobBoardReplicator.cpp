// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_JobBoardReplicator.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//~ FSimAg_JobEntry fast-array callbacks (client side) ------------------------------------------

void FSimAg_JobEntry::PreReplicatedRemove(const FSimAg_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedJobChange(JobId);
	}
}

void FSimAg_JobEntry::PostReplicatedAdd(const FSimAg_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedJobChange(JobId);
	}
}

void FSimAg_JobEntry::PostReplicatedChange(const FSimAg_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedJobChange(JobId);
	}
}

//~ ASimAg_JobBoardReplicator -------------------------------------------------------------------

ASimAg_JobBoardReplicator::ASimAg_JobBoardReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = true; // a single world board is relevant to every connection
	// The board changes only when work is posted/claimed/finished; start dormant and wake on mutation.
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	// Wire the fast-array back-pointer so per-item callbacks can notify us (server and client).
	Jobs.OwnerCarrier = this;
}

void ASimAg_JobBoardReplicator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Re-assert the back-pointer after any sub-object fixup / net construction.
	Jobs.OwnerCarrier = this;
}

void ASimAg_JobBoardReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimAg_JobBoardReplicator, Jobs);
}

void ASimAg_JobBoardReplicator::WakeForChange()
{
	// Move out of dormancy so the just-marked delta is sent, then let the engine re-sleep it.
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

//~ Reads ---------------------------------------------------------------------------------------

const FSimAg_JobEntry* ASimAg_JobBoardReplicator::FindEntry(const FGuid& JobId) const
{
	return Jobs.Entries.FindByPredicate([&JobId](const FSimAg_JobEntry& E) { return E.JobId == JobId; });
}

FSimAg_JobEntry* ASimAg_JobBoardReplicator::FindEntryMutable(const FGuid& JobId)
{
	return Jobs.Entries.FindByPredicate([&JobId](const FSimAg_JobEntry& E) { return E.JobId == JobId; });
}

int32 ASimAg_JobBoardReplicator::CountOpen() const
{
	int32 Count = 0;
	for (const FSimAg_JobEntry& E : Jobs.Entries)
	{
		if (E.State == ESimAg_JobState::Open)
		{
			++Count;
		}
	}
	return Count;
}

//~ Authority mutators --------------------------------------------------------------------------

FGuid ASimAg_JobBoardReplicator::AddJob(const FSimAg_JobRequest& Request)
{
	// AUTHORITY GUARD at top: clients never mutate replicated board state.
	if (!HasAuthority())
	{
		return FGuid();
	}
	if (!Request.JobKind.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg JobBoard AddJob rejected: invalid JobKind."));
		return FGuid();
	}

	const FGuid NewId = FGuid::NewGuid();
	FSimAg_JobEntry& Added = Jobs.Entries.Emplace_GetRef(NewId, Request);
	Jobs.MarkItemDirty(Added);

	WakeForChange();
	OnJobChanged.Broadcast(this, NewId);
	return NewId;
}

bool ASimAg_JobBoardReplicator::ClaimJob(const FGuid& JobId, const FSeam_EntityId& Claimant)
{
	if (!HasAuthority())
	{
		return false;
	}

	FSimAg_JobEntry* Entry = FindEntryMutable(JobId);
	if (!Entry)
	{
		return false;
	}
	// Only an Open posting may be claimed; this is the atomic guard against two agents racing the wire.
	if (Entry->State != ESimAg_JobState::Open)
	{
		return false;
	}

	Entry->State = ESimAg_JobState::Claimed;
	Entry->Claimant = Claimant;
	Jobs.MarkItemDirty(*Entry);

	WakeForChange();
	OnJobChanged.Broadcast(this, JobId);
	return true;
}

bool ASimAg_JobBoardReplicator::CompleteJob(const FGuid& JobId)
{
	if (!HasAuthority())
	{
		return false;
	}

	FSimAg_JobEntry* Entry = FindEntryMutable(JobId);
	if (!Entry || Entry->State == ESimAg_JobState::Completed)
	{
		return false;
	}

	Entry->State = ESimAg_JobState::Completed;
	Jobs.MarkItemDirty(*Entry);

	WakeForChange();
	OnJobChanged.Broadcast(this, JobId);
	return true;
}

bool ASimAg_JobBoardReplicator::CancelJob(const FGuid& JobId)
{
	if (!HasAuthority())
	{
		return false;
	}

	FSimAg_JobEntry* Entry = FindEntryMutable(JobId);
	if (!Entry || Entry->State == ESimAg_JobState::Cancelled)
	{
		return false;
	}

	Entry->State = ESimAg_JobState::Cancelled;
	Entry->Claimant = FSeam_EntityId::Invalid();
	Jobs.MarkItemDirty(*Entry);

	WakeForChange();
	OnJobChanged.Broadcast(this, JobId);
	return true;
}

int32 ASimAg_JobBoardReplicator::PruneTerminal()
{
	if (!HasAuthority())
	{
		return 0;
	}

	const int32 Removed = Jobs.Entries.RemoveAll([](const FSimAg_JobEntry& E)
	{
		return E.State == ESimAg_JobState::Completed || E.State == ESimAg_JobState::Cancelled;
	});

	if (Removed > 0)
	{
		Jobs.MarkArrayDirty();
		WakeForChange();
	}
	return Removed;
}

//~ Client-side change surfacing ----------------------------------------------------------------

void ASimAg_JobBoardReplicator::HandleReplicatedJobChange(const FGuid& JobId)
{
	OnJobChanged.Broadcast(this, JobId);
}
