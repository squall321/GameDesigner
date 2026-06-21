// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Scoreboard/ANet_ScoreboardState.h"
#include "DesignPatternsNetNativeTags.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

// ---- Fast-array item callbacks (client) -----------------------------------------------------------

void FNet_ScoreRowItem::PreReplicatedRemove(const FNet_ScoreRowArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier) { InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(); }
}
void FNet_ScoreRowItem::PostReplicatedAdd(const FNet_ScoreRowArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier) { InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(); }
}
void FNet_ScoreRowItem::PostReplicatedChange(const FNet_ScoreRowArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier) { InArraySerializer.OwnerCarrier->HandleReplicatedScoreChange(); }
}

// ---- Carrier --------------------------------------------------------------------------------------

ANet_ScoreboardState::ANet_ScoreboardState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	NetDormancy = DORM_DormantAll; // static until a score changes
	NetUpdateFrequency = 10.f;
}

void ANet_ScoreboardState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANet_ScoreboardState, Scores);
	DOREPLIFETIME(ANet_ScoreboardState, bResultsFinal);
}

void ANet_ScoreboardState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Scores.OwnerCarrier = this;
}

void ANet_ScoreboardState::BeginPlay()
{
	Super::BeginPlay();
	RegisterSelfAsService();
}

void ANet_ScoreboardState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->UnregisterService(NetNativeTags::Service_Net_Scoreboard);
	}
	Super::EndPlay(EndPlayReason);
}

void ANet_ScoreboardState::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(NetNativeTags::Service_Net_Scoreboard, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

// ---- ISeam_ScoreSource ----------------------------------------------------------------------------

int64 ANet_ScoreboardState::GetScore_Implementation(FGameplayTag Key) const
{
	const FNet_ScoreRowItem* Row = FindRow(Key);
	return Row ? Row->Score : 0;
}

void ANet_ScoreboardState::GetAllScores_Implementation(TArray<FSeam_ScoreRow>& OutRows) const
{
	OutRows.Reset(Scores.Rows.Num());
	for (const FNet_ScoreRowItem& Item : Scores.Rows)
	{
		OutRows.Add(Item.ToSeamRow());
	}
}

bool ANet_ScoreboardState::AreResultsFinal_Implementation() const
{
	return bResultsFinal;
}

// ---- Reads ----------------------------------------------------------------------------------------

const FNet_ScoreRowItem* ANet_ScoreboardState::FindRow(const FGameplayTag& Key) const
{
	return Scores.Rows.FindByPredicate([&Key](const FNet_ScoreRowItem& I){ return I.Key == Key; });
}

FNet_ScoreRowItem* ANet_ScoreboardState::FindRowMutable(const FGameplayTag& Key)
{
	return Scores.Rows.FindByPredicate([&Key](const FNet_ScoreRowItem& I){ return I.Key == Key; });
}

FGameplayTag ANet_ScoreboardState::GetLeadingKey() const
{
	FGameplayTag Best;
	int64 BestScore = TNumericLimits<int64>::Min();
	for (const FNet_ScoreRowItem& Item : Scores.Rows)
	{
		if (Item.Score > BestScore)
		{
			BestScore = Item.Score;
			Best = Item.Key;
		}
	}
	return Best;
}

// ---- Authority mutators ---------------------------------------------------------------------------

bool ANet_ScoreboardState::EnsureBucket(const FGameplayTag& Key, int64 StartingScore, const FText& DisplayName)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_ScoreboardState::EnsureBucket")))
	{
		return false;
	}
	if (FindRowMutable(Key))
	{
		return false;
	}
	FNet_ScoreRowItem Item(Key);
	Item.Score = StartingScore;
	Item.DisplayName = DisplayName;
	Scores.Rows.Add(Item);
	Scores.MarkItemDirty(Scores.Rows.Last());
	WakeForChange();
	OnScoreboardChanged.Broadcast(Key);
	return true;
}

int64 ANet_ScoreboardState::AddScore(const FGameplayTag& Key, int64 Delta)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_ScoreboardState::AddScore")))
	{
		return 0;
	}
	FNet_ScoreRowItem* Row = FindRowMutable(Key);
	if (!Row)
	{
		FNet_ScoreRowItem Item(Key);
		Scores.Rows.Add(Item);
		Row = &Scores.Rows.Last();
	}
	Row->Score += Delta;
	Scores.MarkItemDirty(*Row);
	WakeForChange();
	OnScoreboardChanged.Broadcast(Key);
	return Row->Score;
}

int64 ANet_ScoreboardState::SetScore(const FGameplayTag& Key, int64 NewScore)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_ScoreboardState::SetScore")))
	{
		return 0;
	}
	FNet_ScoreRowItem* Row = FindRowMutable(Key);
	if (!Row)
	{
		FNet_ScoreRowItem Item(Key);
		Scores.Rows.Add(Item);
		Row = &Scores.Rows.Last();
	}
	Row->Score = NewScore;
	Scores.MarkItemDirty(*Row);
	WakeForChange();
	OnScoreboardChanged.Broadcast(Key);
	return Row->Score;
}

void ANet_ScoreboardState::ResetScores()
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_ScoreboardState::ResetScores")))
	{
		return;
	}
	for (FNet_ScoreRowItem& Item : Scores.Rows)
	{
		Item.Score = 0;
		Scores.MarkItemDirty(Item);
	}
	bResultsFinal = false;
	WakeForChange();
	OnScoreboardChanged.Broadcast(FGameplayTag());
}

void ANet_ScoreboardState::SetResultsFinal(bool bInFinal)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_ScoreboardState::SetResultsFinal")))
	{
		return;
	}
	if (bResultsFinal != bInFinal)
	{
		bResultsFinal = bInFinal;
		WakeForChange();
		OnScoreboardChanged.Broadcast(FGameplayTag());
	}
}

// ---- Change plumbing ------------------------------------------------------------------------------

void ANet_ScoreboardState::HandleReplicatedScoreChange()
{
	OnScoreboardChanged.Broadcast(FGameplayTag());
}

void ANet_ScoreboardState::OnRep_ResultsFinal()
{
	OnScoreboardChanged.Broadcast(FGameplayTag());
}

void ANet_ScoreboardState::WakeForChange()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
	ForceNetUpdate();
}
