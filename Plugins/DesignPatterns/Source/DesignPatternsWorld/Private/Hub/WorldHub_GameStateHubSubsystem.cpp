// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hub/WorldHub_GameStateHubSubsystem.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "Save/WorldHub_SaveGame.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

void UWorldHub_GameStateHubSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("[WorldHub] Persistent game-state hub initialized."));
}

void UWorldHub_GameStateHubSubsystem::Deinitialize()
{
	Persistent.Reset();
	Super::Deinitialize();
}

//~ Persistent key/value -----------------------------------------------------------------------

void UWorldHub_GameStateHubSubsystem::SetPersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value)
{
	if (!Key.IsValid())
	{
		return;
	}
	Persistent.Add(FPersistentKey{ Scope, Key }, Value);
}

bool UWorldHub_GameStateHubSubsystem::GetPersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FWorldHub_FlagValue& Out) const
{
	if (const FWorldHub_FlagValue* Found = Persistent.Find(FPersistentKey{ Scope, Key }))
	{
		Out = *Found;
		return true;
	}
	return false;
}

bool UWorldHub_GameStateHubSubsystem::RemovePersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope)
{
	return Persistent.Remove(FPersistentKey{ Scope, Key }) > 0;
}

void UWorldHub_GameStateHubSubsystem::ClearAll()
{
	Persistent.Reset();
}

//~ World-hub bridge ---------------------------------------------------------------------------

void UWorldHub_GameStateHubSubsystem::SeedWorldHub(UWorldHub_StateHubSubsystem* WorldHub) const
{
	if (!WorldHub)
	{
		return;
	}
	// Seeding mutates authoritative state — only do it on the server side.
	if (!WorldHub->HasWorldAuthority())
	{
		return;
	}

	for (const TPair<FPersistentKey, FWorldHub_FlagValue>& Pair : Persistent)
	{
		WorldHub->SetValue(Pair.Key.Key, Pair.Key.Scope, Pair.Value);
	}
	UE_LOG(LogDP, Verbose, TEXT("[WorldHub] Seeded world hub from %d persistent slots."), Persistent.Num());
}

void UWorldHub_GameStateHubSubsystem::ReceiveFlush(const UWorldHub_StateHubSubsystem* WorldHub)
{
	if (!WorldHub)
	{
		return;
	}
	const UWorldHub_FlagRegistry* Registry = WorldHub->GetRegistry();
	if (!Registry)
	{
		return;
	}

	TArray<UWorldHub_FlagRegistry::FSlotRecord> Records;
	Registry->CaptureSaveSlots(Records);
	for (const UWorldHub_FlagRegistry::FSlotRecord& Record : Records)
	{
		Persistent.Add(FPersistentKey{ Record.Scope, Record.Key }, Record.Value);
	}
	UE_LOG(LogDP, Verbose, TEXT("[WorldHub] Received flush of %d save slots into persistent store."), Records.Num());
}

void UWorldHub_GameStateHubSubsystem::ReceiveSnapshot(const FWorldHub_Snapshot& Snapshot)
{
	for (const FWorldHub_SnapshotEntry& Entry : Snapshot.Entries)
	{
		if (Entry.Key.IsValid())
		{
			Persistent.Add(FPersistentKey{ Entry.Scope, Entry.Key }, Entry.Value);
		}
	}
}

void UWorldHub_GameStateHubSubsystem::BuildSnapshot(FWorldHub_Snapshot& Out) const
{
	Out.Reset();
	Out.Entries.Reserve(Persistent.Num());
	for (const TPair<FPersistentKey, FWorldHub_FlagValue>& Pair : Persistent)
	{
		Out.Entries.Emplace(Pair.Key.Scope, Pair.Key.Key, Pair.Value);
	}
}

//~ Save bridge --------------------------------------------------------------------------------

UWorldHub_SaveGame* UWorldHub_GameStateHubSubsystem::BuildSaveObject() const
{
	UWorldHub_SaveGame* Save = NewObject<UWorldHub_SaveGame>(GetTransientPackage());
	BuildSnapshot(Save->Snapshot);
	Save->SaveSchemaVersion = UWorldHub_SaveGame::CurrentSaveSchemaVersion;
	return Save;
}

void UWorldHub_GameStateHubSubsystem::ApplyFromSave(UWorldHub_SaveGame* SaveObject)
{
	if (!SaveObject)
	{
		return;
	}
	Persistent.Reset();
	ReceiveSnapshot(SaveObject->Snapshot);
	UE_LOG(LogDP, Log, TEXT("[WorldHub] Applied %d persistent slots from save."), Persistent.Num());
}

void UWorldHub_GameStateHubSubsystem::SaveToSlotAsync(const FString& Slot)
{
	UDP_SaveGameSubsystem* SaveSys =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
	if (!SaveSys)
	{
		OnPersistentSaved.Broadcast(Slot, EDP_SaveResult::InvalidArgument);
		return;
	}

	UWorldHub_SaveGame* Save = BuildSaveObject();

	// Capture a weak self so the completion callback is safe if the subsystem is gone.
	TWeakObjectPtr<UWorldHub_GameStateHubSubsystem> WeakThis(this);
	FDP_SaveCallbackDynamic OnComplete;
	OnComplete.BindLambda([WeakThis](FString CompletedSlot, EDP_SaveResult Result)
	{
		if (UWorldHub_GameStateHubSubsystem* Self = WeakThis.Get())
		{
			Self->OnPersistentSaved.Broadcast(CompletedSlot, Result);
		}
	});

	SaveSys->SaveAsync(Slot, Save, OnComplete);
}

void UWorldHub_GameStateHubSubsystem::LoadFromSlotAsync(const FString& Slot)
{
	UDP_SaveGameSubsystem* SaveSys =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
	if (!SaveSys)
	{
		OnPersistentLoaded.Broadcast(Slot, EDP_SaveResult::InvalidArgument);
		return;
	}

	TWeakObjectPtr<UWorldHub_GameStateHubSubsystem> WeakThis(this);
	FDP_LoadCallbackDynamic OnComplete;
	OnComplete.BindLambda([WeakThis](FString CompletedSlot, EDP_SaveResult Result, UDP_SaveGame* SaveObject)
	{
		UWorldHub_GameStateHubSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (Result == EDP_SaveResult::Success)
		{
			Self->ApplyFromSave(Cast<UWorldHub_SaveGame>(SaveObject));
		}
		Self->OnPersistentLoaded.Broadcast(CompletedSlot, Result);
	});

	SaveSys->LoadAsync(Slot, OnComplete);
}

//~ Debug --------------------------------------------------------------------------------------

FString UWorldHub_GameStateHubSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("WorldHub GameState | Persistent=%d"), Persistent.Num());
}
