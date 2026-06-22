// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Profile/SaveX_ProfileSubsystem.h"
#include "Profile/DP_ProfileSaveGame.h"
#include "Storage/SaveX_StorageSubsystem.h"
#include "Storage/SaveX_ContainerHeader.h"   // ESaveX_ContainerFlag
#include "Settings/SaveX_StorageDeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"          // FDP_SubsystemStatics
#include "Save/DPSaveGame.h"
#include "Persist/Seam_Persistable.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                       // TActorIterator
#include "GameFramework/Actor.h"

void USaveX_ProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Construct an empty profile up front so GetProfile() is never null; a subsequent LoadProfile populates
	// it from disk if a profile exists.
	CachedProfile = ConstructProfileObject();
	UE_LOG(LogDPSave, Log, TEXT("[Profile] Initialized (slot='%s')."), *GetProfileSlotName());
}

void USaveX_ProfileSubsystem::Deinitialize()
{
	CachedProfile = nullptr;
	Super::Deinitialize();
}

USaveX_StorageSubsystem* USaveX_ProfileSubsystem::GetStorage() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<USaveX_StorageSubsystem>(
		const_cast<USaveX_ProfileSubsystem*>(this));
}

UDP_ProfileSaveGame* USaveX_ProfileSubsystem::ConstructProfileObject() const
{
	TSubclassOf<UDP_ProfileSaveGame> ProfileClass = UDP_ProfileSaveGame::StaticClass();
	if (const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get())
	{
		if (Settings->ProfileSaveGameClass.IsValid())
		{
			if (UClass* Resolved = Settings->ProfileSaveGameClass.TryLoadClass<UDP_ProfileSaveGame>())
			{
				ProfileClass = Resolved;
			}
		}
	}
	UDP_ProfileSaveGame* Object = NewObject<UDP_ProfileSaveGame>(GetTransientPackage(), ProfileClass);
	if (!Object)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Profile] Failed to construct profile object of class '%s'."),
			*GetNameSafe(ProfileClass));
	}
	return Object;
}

FString USaveX_ProfileSubsystem::GetProfileSlotName() const
{
	const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get();
	return Settings ? Settings->GetEffectiveProfileSlotName() : TEXT("DPProfile");
}

bool USaveX_ProfileSubsystem::IsProfileKind(FGameplayTag Kind) const
{
	if (!Kind.IsValid())
	{
		return false;
	}
	if (const USaveX_StorageDeveloperSettings* Settings = USaveX_StorageDeveloperSettings::Get())
	{
		// HasTag matches hierarchically: a PARENT kind configured in settings (e.g. SaveX.Persist.Kind.Profile)
		// captures every CHILD participant kind beneath it, so designers list one root rather than every leaf.
		return Settings->ProfilePersistenceKinds.HasTag(Kind);
	}
	return false;
}

bool USaveX_ProfileSubsystem::HasSaveAuthority() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return true; // no context to deny on; per-participant contracts still guard
	}
	const UWorld* World = GI->GetWorld();
	return !World || World->GetNetMode() != NM_Client;
}

void USaveX_ProfileSubsystem::GatherProfileParticipants()
{
	if (!CachedProfile)
	{
		CachedProfile = ConstructProfileObject();
		if (!CachedProfile)
		{
			return;
		}
	}

	// Authority guard at the TOP: a pure client does not gather authoritative world state.
	if (!HasSaveAuthority())
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Profile] Gather skipped on non-authority."));
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	// Start clean so a re-gather never accumulates stale records.
	CachedProfile->ResetGatheredRecords();

	int32 Gathered = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		TArray<UObject*, TInlineAllocator<4>> Participants;
		if (Actor->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
		{
			Participants.Add(Actor);
		}
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (IsValid(Comp) && Comp->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
			{
				Participants.Add(Comp);
			}
		}

		for (UObject* Participant : Participants)
		{
			if (!IsValid(Participant))
			{
				continue;
			}
			const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Participant);
			if (!IsProfileKind(Kind))
			{
				continue; // belongs to a world save, not the profile partition
			}
			FInstancedStruct Record;
			ISeam_Persistable::Execute_CaptureState(Participant, Record);
			if (Record.IsValid())
			{
				CachedProfile->DepositRecord(Kind, Record);
				++Gathered;
			}
		}
	}

	UE_CLOG(Gathered > 0, LogDPSave, Verbose, TEXT("[Profile] Gathered %d profile-kind record(s)."), Gathered);
}

void USaveX_ProfileSubsystem::SaveProfile()
{
	GatherProfileParticipants();

	USaveX_StorageSubsystem* Storage = GetStorage();
	if (!Storage || !CachedProfile)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Profile] SaveProfile: storage/profile unavailable."));
		OnProfileSaved.Broadcast(false);
		return;
	}

	CachedProfile->DisplayName = TEXT("Profile");

	TWeakObjectPtr<USaveX_ProfileSubsystem> WeakThis(this);
	FSaveX_StorageSaveDone OnDone;
	OnDone.BindLambda([WeakThis](const FString& /*Slot*/, ESaveX_StorageResult Result)
	{
		if (USaveX_ProfileSubsystem* Self = WeakThis.Get())
		{
			const bool bSuccess = (Result == ESaveX_StorageResult::Success);
			UE_CLOG(!bSuccess, LogDPSave, Warning, TEXT("[Profile] SaveProfile failed (result=%d)."), static_cast<int32>(Result));
			Self->OnProfileSaved.Broadcast(bSuccess);
		}
	});

	Storage->SaveWrapped(GetProfileSlotName(), CachedProfile, /*bIsAutosave=*/false,
		static_cast<uint8>(ESaveX_ContainerFlag::IsProfile), MoveTemp(OnDone));
}

void USaveX_ProfileSubsystem::LoadProfile()
{
	USaveX_StorageSubsystem* Storage = GetStorage();
	if (!Storage)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Profile] LoadProfile: storage unavailable."));
		OnProfileLoaded.Broadcast(false, CachedProfile);
		return;
	}

	TWeakObjectPtr<USaveX_ProfileSubsystem> WeakThis(this);
	FSaveX_StorageLoadDone OnDone;
	OnDone.BindLambda([WeakThis](const FString& /*Slot*/, ESaveX_StorageResult Result, UDP_SaveGame* SaveObject)
	{
		USaveX_ProfileSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (Result == ESaveX_StorageResult::Success)
		{
			if (UDP_ProfileSaveGame* Loaded = Cast<UDP_ProfileSaveGame>(SaveObject))
			{
				Self->CachedProfile = Loaded;
				UE_LOG(LogDPSave, Log, TEXT("[Profile] Loaded profile (%d shared record(s))."),
					Loaded->SharedRecords.Num());
				Self->OnProfileLoaded.Broadcast(true, Loaded);
				return;
			}
			UE_LOG(LogDPSave, Warning, TEXT("[Profile] Loaded object was not a UDP_ProfileSaveGame."));
		}
		else if (Result == ESaveX_StorageResult::SlotNotFound)
		{
			// No profile yet: keep the empty cached profile so callers always have a valid object.
			UE_LOG(LogDPSave, Log, TEXT("[Profile] No existing profile; using empty profile."));
		}
		else
		{
			UE_LOG(LogDPSave, Warning, TEXT("[Profile] LoadProfile failed (result=%d)."), static_cast<int32>(Result));
		}
		Self->OnProfileLoaded.Broadcast(false, Self->CachedProfile);
	});

	Storage->LoadWrapped(GetProfileSlotName(), MoveTemp(OnDone));
}

void USaveX_ProfileSubsystem::MergeSharedDataIntoWorldSave(UDP_SaveGame* WorldSave) const
{
	if (!WorldSave || !CachedProfile)
	{
		return;
	}

	// If the world save aggregates records (implements ISeam_Persistable), hand it the profile aggregate so
	// a world load can read profile unlocks without a separate profile read. Otherwise this is a no-op (the
	// project save subclass reads the profile subsystem directly in its own OnPreSave).
	if (WorldSave->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
	{
		FInstancedStruct Aggregate;
		ISeam_Persistable::Execute_CaptureState(CachedProfile, Aggregate);
		if (Aggregate.IsValid())
		{
			ISeam_Persistable::Execute_RestoreState(WorldSave, Aggregate);
		}
	}
}

FString USaveX_ProfileSubsystem::GetDPDebugString_Implementation() const
{
	const int32 Records = CachedProfile ? CachedProfile->SharedRecords.Num() : 0;
	const int32 Unlocks = CachedProfile ? CachedProfile->Unlocks.Num() : 0;
	return FString::Printf(TEXT("Profile: slot='%s' records=%d unlocks=%d"),
		*GetProfileSlotName(), Records, Unlocks);
}
