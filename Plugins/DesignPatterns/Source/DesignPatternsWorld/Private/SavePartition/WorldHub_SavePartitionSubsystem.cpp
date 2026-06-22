// Copyright DesignPatterns plugin. All Rights Reserved.

#include "SavePartition/WorldHub_SavePartitionSubsystem.h"
#include "SavePartition/WorldHub_SavePartitionPolicyDataAsset.h"
#include "Hub/WorldHub_GameStateHubSubsystem.h"
#include "Save/WorldHub_SaveGame.h"

#include "Save/DPSaveGameSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

void UWorldHub_SavePartitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("[WorldHub] Save-partition subsystem initialized."));
}

void UWorldHub_SavePartitionSubsystem::Deinitialize()
{
	PersistentHub.Reset();
	Super::Deinitialize();
}

UWorldHub_GameStateHubSubsystem* UWorldHub_SavePartitionSubsystem::ResolvePersistentHub()
{
	if (UWorldHub_GameStateHubSubsystem* Cached = PersistentHub.Get())
	{
		return Cached;
	}
	UWorldHub_GameStateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UWorldHub_GameStateHubSubsystem>(this);
	PersistentHub = Resolved;
	return Resolved;
}

//~ Partition resolution -----------------------------------------------------------------------

FGameplayTag UWorldHub_SavePartitionSubsystem::ResolvePartitionFor(const FWorldHub_SlotAddress& Address) const
{
	if (!PartitionPolicy)
	{
		return FGameplayTag();
	}
	for (const FWorldHub_SavePartitionDef& Def : PartitionPolicy->Partitions)
	{
		const bool bKeyOk = !Def.KeyRoot.IsValid() || Address.Key.MatchesTag(Def.KeyRoot);
		const bool bScopeOk = !Def.bRestrictToScopeType || Address.Scope.ScopeType == Def.ScopeType;
		if (bKeyOk && bScopeOk)
		{
			return Def.PartitionId;
		}
	}
	return FGameplayTag();
}

bool UWorldHub_SavePartitionSubsystem::EntryBelongsToPartition(const FWorldHub_SnapshotEntry& Entry, FGameplayTag PartitionId) const
{
	return ResolvePartitionFor(FWorldHub_SlotAddress(Entry.Scope, Entry.Key)) == PartitionId;
}

FString UWorldHub_SavePartitionSubsystem::ComposePartitionSlot(FGameplayTag PartitionId, const FString& BaseSlot) const
{
	if (PartitionPolicy)
	{
		if (const FWorldHub_SavePartitionDef* Def = PartitionPolicy->FindPartition(PartitionId))
		{
			if (!Def->SlotSuffix.IsEmpty())
			{
				return BaseSlot + Def->SlotSuffix;
			}
		}
	}
	// Defensive fallback: derive a deterministic per-partition slot from the leaf tag name.
	return BaseSlot + TEXT("_") + PartitionId.GetTagName().ToString();
}

//~ Snapshot subset ----------------------------------------------------------------------------

void UWorldHub_SavePartitionSubsystem::GetPartitionSnapshot(FGameplayTag PartitionId, FWorldHub_Snapshot& Out) const
{
	Out.Reset();
	const UWorldHub_GameStateHubSubsystem* Hub =
		const_cast<UWorldHub_SavePartitionSubsystem*>(this)->ResolvePersistentHub();
	if (!Hub)
	{
		return;
	}

	// Reuse the existing full-snapshot build, then filter to the partition's subset.
	FWorldHub_Snapshot Full;
	Hub->BuildSnapshot(Full);
	Out.SnapshotVersion = Full.SnapshotVersion;
	for (const FWorldHub_SnapshotEntry& Entry : Full.Entries)
	{
		if (EntryBelongsToPartition(Entry, PartitionId))
		{
			Out.Entries.Add(Entry);
		}
	}
}

void UWorldHub_SavePartitionSubsystem::MergePartitionSnapshot(FGameplayTag PartitionId, const FWorldHub_Snapshot& In)
{
	UWorldHub_GameStateHubSubsystem* Hub = ResolvePersistentHub();
	if (!Hub)
	{
		return;
	}
	// Merge only the entries that actually belong to this partition (defensive against a tampered file),
	// reusing the persistent hub's existing per-slot store via ReceiveSnapshot of the filtered subset.
	FWorldHub_Snapshot Filtered;
	Filtered.SnapshotVersion = In.SnapshotVersion;
	for (const FWorldHub_SnapshotEntry& Entry : In.Entries)
	{
		if (EntryBelongsToPartition(Entry, PartitionId))
		{
			Filtered.Entries.Add(Entry);
		}
	}
	Hub->ReceiveSnapshot(Filtered);
}

void UWorldHub_SavePartitionSubsystem::ApplyKeyMigrations(FWorldHub_Snapshot& Snapshot) const
{
	if (!PartitionPolicy || PartitionPolicy->KeyMigrations.Num() == 0)
	{
		return;
	}
	// Build a fast rename map (FromKey -> ToKey). Invalid ToKey means "drop this slot".
	TMap<FGameplayTag, FGameplayTag> Renames;
	Renames.Reserve(PartitionPolicy->KeyMigrations.Num());
	for (const FWorldHub_KeyMigration& Mig : PartitionPolicy->KeyMigrations)
	{
		if (Mig.FromKey.IsValid())
		{
			Renames.Add(Mig.FromKey, Mig.ToKey);
		}
	}

	TArray<FWorldHub_SnapshotEntry> Migrated;
	Migrated.Reserve(Snapshot.Entries.Num());
	for (FWorldHub_SnapshotEntry& Entry : Snapshot.Entries)
	{
		if (const FGameplayTag* To = Renames.Find(Entry.Key))
		{
			if (!To->IsValid())
			{
				continue; // Dropped by migration.
			}
			Entry.Key = *To;
		}
		Migrated.Add(Entry);
	}
	Snapshot.Entries = MoveTemp(Migrated);
}

//~ Async save / load --------------------------------------------------------------------------

void UWorldHub_SavePartitionSubsystem::SavePartitionAsync(FGameplayTag PartitionId, const FString& Slot)
{
	UDP_SaveGameSubsystem* SaveSys =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
	if (!SaveSys)
	{
		OnPartitionSaved.Broadcast(PartitionId, Slot, /*bSuccess=*/false);
		return;
	}

	// Build a save object carrying only this partition's subset.
	UWorldHub_SaveGame* Save = NewObject<UWorldHub_SaveGame>(this);
	GetPartitionSnapshot(PartitionId, Save->Snapshot);

	const FString PartitionSlot = ComposePartitionSlot(PartitionId, Slot);

	TWeakObjectPtr<UWorldHub_SavePartitionSubsystem> WeakThis(this);
	FDP_SaveCallbackDynamic OnComplete;
	OnComplete.BindLambda([WeakThis, PartitionId](FString CompletedSlot, EDP_SaveResult Result)
	{
		if (UWorldHub_SavePartitionSubsystem* Self = WeakThis.Get())
		{
			Self->OnPartitionSaved.Broadcast(PartitionId, CompletedSlot, Result == EDP_SaveResult::Success);
		}
	});

	SaveSys->SaveAsync(PartitionSlot, Save, OnComplete);
}

void UWorldHub_SavePartitionSubsystem::LoadPartitionAsync(FGameplayTag PartitionId, const FString& Slot)
{
	UDP_SaveGameSubsystem* SaveSys =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
	if (!SaveSys)
	{
		OnPartitionLoaded.Broadcast(PartitionId, Slot, /*bSuccess=*/false);
		return;
	}

	const FString PartitionSlot = ComposePartitionSlot(PartitionId, Slot);

	TWeakObjectPtr<UWorldHub_SavePartitionSubsystem> WeakThis(this);
	FDP_LoadCallbackDynamic OnComplete;
	OnComplete.BindLambda([WeakThis, PartitionId](FString CompletedSlot, EDP_SaveResult Result, UDP_SaveGame* SaveObject)
	{
		UWorldHub_SavePartitionSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		bool bSuccess = false;
		if (Result == EDP_SaveResult::Success)
		{
			if (UWorldHub_SaveGame* HubSave = Cast<UWorldHub_SaveGame>(SaveObject))
			{
				FWorldHub_Snapshot Snapshot = HubSave->Snapshot;
				// Migrate keys before merging (handles older partition schemas non-destructively).
				Self->ApplyKeyMigrations(Snapshot);
				Self->MergePartitionSnapshot(PartitionId, Snapshot);
				bSuccess = true;
			}
		}
		Self->OnPartitionLoaded.Broadcast(PartitionId, CompletedSlot, bSuccess);
	});

	SaveSys->LoadAsync(PartitionSlot, OnComplete);
}

//~ Debug --------------------------------------------------------------------------------------

FString UWorldHub_SavePartitionSubsystem::GetDPDebugString_Implementation() const
{
	const int32 NumPartitions = PartitionPolicy ? PartitionPolicy->Partitions.Num() : 0;
	return FString::Printf(TEXT("WorldHub SavePartition | Partitions=%d Hub=%s"),
		NumPartitions, PersistentHub.IsValid() ? TEXT("resolved") : TEXT("lazy"));
}
