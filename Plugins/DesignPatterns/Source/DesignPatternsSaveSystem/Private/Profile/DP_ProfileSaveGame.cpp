// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Profile/DP_ProfileSaveGame.h"
#include "Core/DPLog.h"

void UDP_ProfileSaveGame::CaptureState_Implementation(FInstancedStruct& Out) const
{
	// Emit the whole shared store as one aggregate record so a generic scatter can route it.
	FDP_ProfileAggregateRecord Aggregate;
	Aggregate.SharedRecords = SharedRecords;
	Aggregate.RecordKinds = RecordKinds;
	Aggregate.Unlocks = Unlocks;
	Out = FInstancedStruct::Make(Aggregate);
}

void UDP_ProfileSaveGame::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (!In.IsValid())
	{
		return;
	}

	// Two accepted shapes:
	//  (a) the aggregate record (scatter back into this object), or
	//  (b) a single participant record from the generic gather path (deposited under an unknown kind).
	if (const FDP_ProfileAggregateRecord* Aggregate = In.GetPtr<FDP_ProfileAggregateRecord>())
	{
		SharedRecords = Aggregate->SharedRecords;
		RecordKinds = Aggregate->RecordKinds;
		Unlocks = Aggregate->Unlocks;
		// Keep the parallel arrays in lockstep defensively.
		RecordKinds.SetNum(SharedRecords.Num());
		return;
	}

	// Generic gather path: we do not know the kind here, so deposit under an invalid tag. The profile
	// subsystem's own gather uses DepositRecord with the real kind; this is the fallback for the generic
	// slot-manager flow when the profile object is used as a plain aggregate.
	DepositRecord(FGameplayTag(), In);
}

FGameplayTag UDP_ProfileSaveGame::GetPersistenceKind_Implementation() const
{
	return ProfileKind;
}

void UDP_ProfileSaveGame::DepositRecord(FGameplayTag Kind, const FInstancedStruct& Record)
{
	if (!Record.IsValid())
	{
		return;
	}

	// Replace an existing record of the same (valid) kind so the profile holds at most one record per kind.
	if (Kind.IsValid())
	{
		const int32 Existing = RecordKinds.IndexOfByKey(Kind);
		if (Existing != INDEX_NONE && SharedRecords.IsValidIndex(Existing))
		{
			SharedRecords[Existing] = Record;
			return;
		}
	}

	SharedRecords.Add(Record);
	RecordKinds.Add(Kind);
}

bool UDP_ProfileSaveGame::FindSharedRecordByKind(FGameplayTag Kind, FInstancedStruct& Out) const
{
	const int32 Index = RecordKinds.IndexOfByKey(Kind);
	if (Index != INDEX_NONE && SharedRecords.IsValidIndex(Index))
	{
		Out = SharedRecords[Index];
		return true;
	}
	Out.Reset();
	return false;
}

void UDP_ProfileSaveGame::ResetGatheredRecords()
{
	SharedRecords.Reset();
	RecordKinds.Reset();
}
