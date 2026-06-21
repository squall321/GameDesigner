// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Score/GM_ScoreCarrier.h"

#include "DesignPatternsGameModeModule.h"
#include "Core/DPLog.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//================================================================================================
// FGM_ScoreItem — fast-array item replication callbacks (client side)
//================================================================================================

void FGM_ScoreItem::PreReplicatedRemove(const FGM_ScoreArray& InArraySerializer)
{
	// A bucket disappearing is a board change for this key; surface it so UIs drop the row.
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(Key);
	}
}

void FGM_ScoreItem::PostReplicatedAdd(const FGM_ScoreArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(Key);
	}
}

void FGM_ScoreItem::PostReplicatedChange(const FGM_ScoreArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(Key);
	}
}

//================================================================================================
// AGM_ScoreCarrier
//================================================================================================

AGM_ScoreCarrier::AGM_ScoreCarrier()
{
	// The carrier is the replicated state-of-record for the scoreboard (HARD RULE 5: replicated state on
	// an AInfo carrier, never on the subsystem).
	bReplicates = true;
	bAlwaysRelevant = true; // Every client needs the scoreboard; it is cheap (delta-serialized fast array).
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	// Net dormancy: a static scoreboard costs zero per-frame bandwidth. We flush dormancy explicitly when a
	// score actually changes (WakeForChange), then the actor re-enters dormancy until the next change.
	NetDormancy = DORM_DormantAll;
}

void AGM_ScoreCarrier::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Wire the fast array's back-pointer on BOTH server and client so the per-item replication callbacks can
	// notify this carrier (the pointer itself is NotReplicated/Transient).
	Scores.OwnerCarrier = this;
}

void AGM_ScoreCarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGM_ScoreCarrier, Scores);
	DOREPLIFETIME(AGM_ScoreCarrier, bResultsFinal);
}

//~ ISeam_ScoreSource (read; safe on clients) -----------------------------------------------------

int64 AGM_ScoreCarrier::GetScore_Implementation(FGameplayTag Key) const
{
	if (const FGM_ScoreItem* Row = FindRow(Key))
	{
		return Row->Score;
	}
	return 0;
}

void AGM_ScoreCarrier::GetAllScores_Implementation(TArray<FSeam_ScoreRow>& OutRows) const
{
	OutRows.Reset(Scores.Rows.Num());
	for (const FGM_ScoreItem& Item : Scores.Rows)
	{
		OutRows.Add(Item.ToSeamRow());
	}
}

bool AGM_ScoreCarrier::AreResultsFinal_Implementation() const
{
	return bResultsFinal;
}

//~ Authority mutators (each early-returns on clients) --------------------------------------------

bool AGM_ScoreCarrier::EnsureBucket(const FGameplayTag& Key, int64 StartingScore, const FText& DisplayName)
{
	// HARD RULE 5: authority guard at the TOP.
	if (!HasAuthority())
	{
		return false;
	}
	if (!Key.IsValid())
	{
		return false;
	}

	if (FGM_ScoreItem* Existing = FindRowMutable(Key))
	{
		// Already present; refresh the display label if one was supplied (does not reset the score).
		if (!DisplayName.IsEmpty() && !Existing->DisplayName.EqualTo(DisplayName))
		{
			Existing->DisplayName = DisplayName;
			Scores.MarkItemDirty(*Existing);
			WakeForChange();
		}
		return false;
	}

	FGM_ScoreItem NewItem(Key);
	NewItem.Score = StartingScore;
	NewItem.DisplayName = DisplayName;
	FGM_ScoreItem& Added = Scores.Rows.Add_GetRef(NewItem);
	Scores.MarkItemDirty(Added);
	WakeForChange();

	// Surface locally on the authority too (the OnRep path only runs on clients).
	OnScoreChanged.Broadcast(this, Key);
	return true;
}

int64 AGM_ScoreCarrier::AddScore(const FGameplayTag& Key, int64 Delta)
{
	if (!HasAuthority())
	{
		return 0;
	}
	if (!Key.IsValid())
	{
		return 0;
	}

	FGM_ScoreItem* Row = FindRowMutable(Key);
	if (!Row)
	{
		// Create the bucket at 0 first, then apply the delta (so an ad-hoc key starts from zero).
		FGM_ScoreItem NewItem(Key);
		Row = &Scores.Rows.Add_GetRef(NewItem);
	}

	Row->Score += Delta;
	Scores.MarkItemDirty(*Row);
	WakeForChange();
	OnScoreChanged.Broadcast(this, Key);
	return Row->Score;
}

int64 AGM_ScoreCarrier::SetScore(const FGameplayTag& Key, int64 NewScore)
{
	if (!HasAuthority())
	{
		return 0;
	}
	if (!Key.IsValid())
	{
		return 0;
	}

	FGM_ScoreItem* Row = FindRowMutable(Key);
	if (!Row)
	{
		FGM_ScoreItem NewItem(Key);
		Row = &Scores.Rows.Add_GetRef(NewItem);
	}

	if (Row->Score != NewScore)
	{
		Row->Score = NewScore;
		Scores.MarkItemDirty(*Row);
		WakeForChange();
		OnScoreChanged.Broadcast(this, Key);
	}
	return Row->Score;
}

void AGM_ScoreCarrier::ResetScores()
{
	if (!HasAuthority())
	{
		return;
	}

	bool bChanged = false;
	for (FGM_ScoreItem& Item : Scores.Rows)
	{
		if (Item.Score != 0)
		{
			Item.Score = 0;
			Scores.MarkItemDirty(Item);
			bChanged = true;
		}
	}

	if (bResultsFinal)
	{
		bResultsFinal = false;
		bChanged = true;
	}

	if (bChanged)
	{
		WakeForChange();
		// Board-wide change: broadcast with an invalid key per the delegate contract.
		OnScoreChanged.Broadcast(this, FGameplayTag());
	}
}

void AGM_ScoreCarrier::SetResultsFinal(bool bInFinal)
{
	if (!HasAuthority())
	{
		return;
	}
	if (bResultsFinal == bInFinal)
	{
		return;
	}

	bResultsFinal = bInFinal;
	WakeForChange();
	// Board-wide change (results lock); invalid key signals a board-level update.
	OnScoreChanged.Broadcast(this, FGameplayTag());
}

//~ Reads (client-safe) ---------------------------------------------------------------------------

const FGM_ScoreItem* AGM_ScoreCarrier::FindRow(const FGameplayTag& Key) const
{
	if (!Key.IsValid())
	{
		return nullptr;
	}
	for (const FGM_ScoreItem& Item : Scores.Rows)
	{
		if (Item.Key == Key)
		{
			return &Item;
		}
	}
	return nullptr;
}

FGameplayTag AGM_ScoreCarrier::GetLeadingKey() const
{
	FGameplayTag BestKey;
	int64 BestScore = MIN_int64;
	for (const FGM_ScoreItem& Item : Scores.Rows)
	{
		if (Item.Score > BestScore)
		{
			BestScore = Item.Score;
			BestKey = Item.Key;
		}
	}
	return BestKey;
}

void AGM_ScoreCarrier::HandleReplicatedScoreChange(const FGameplayTag& Key)
{
	// Called from the fast-array item callbacks on clients — surface the replicated change to UI listeners.
	OnScoreChanged.Broadcast(this, Key);
}

//~ Internals -------------------------------------------------------------------------------------

void AGM_ScoreCarrier::OnRep_ResultsFinal()
{
	// Board-wide change for results UIs; invalid key signals the whole board (not a single bucket).
	OnScoreChanged.Broadcast(this, FGameplayTag());
}

FGM_ScoreItem* AGM_ScoreCarrier::FindRowMutable(const FGameplayTag& Key)
{
	if (!Key.IsValid())
	{
		return nullptr;
	}
	for (FGM_ScoreItem& Item : Scores.Rows)
	{
		if (Item.Key == Key)
		{
			return &Item;
		}
	}
	return nullptr;
}

void AGM_ScoreCarrier::WakeForChange()
{
	// Flush dormancy so the just-dirtied delta replicates this frame, then the actor re-enters DORM_DormantAll
	// on its own once the change has gone out. FlushNetDormancy is authority-only-meaningful and a no-op off
	// the server, so it is safe to call from the guarded mutators.
	if (HasAuthority())
	{
		FlushNetDormancy();
	}
}
