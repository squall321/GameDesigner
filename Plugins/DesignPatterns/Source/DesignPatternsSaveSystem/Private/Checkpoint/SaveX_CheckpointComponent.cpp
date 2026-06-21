// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Checkpoint/SaveX_CheckpointComponent.h"

#include "Settings/SaveX_DeveloperSettings.h"
#include "SaveX_ServiceKeys.h"
#include "Autosave/SaveX_AutosaveSubsystem.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Save/DPSaveGame.h"
#include "Save/DPSaveGameSubsystem.h"

#include "Persist/Seam_SaveSlotManager.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USaveX_CheckpointComponent::USaveX_CheckpointComponent()
{
	// No tick: the component is purely event-driven (volume overlap / explicit calls / restore on load).
	PrimaryComponentTick.bCanEverTick = false;

	// We carry a small replicated marker (checkpoint id + transform) so clients can show feedback.
	SetIsReplicatedByDefault(true);
}

void USaveX_CheckpointComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(USaveX_CheckpointComponent, bHasCheckpoint);
	DOREPLIFETIME(USaveX_CheckpointComponent, LastCheckpointId);
	DOREPLIFETIME(USaveX_CheckpointComponent, LastCheckpointTransform);
}

void USaveX_CheckpointComponent::BeginPlay()
{
	Super::BeginPlay();

	// Nothing to spin up: resolution of the save backend is lazy (on first record/restore) so this component
	// is safe to place on actors that exist before the save services are registered.
}

bool USaveX_CheckpointComponent::RecordCheckpoint(FGameplayTag CheckpointId)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// AUTHORITY GUARD: replicated state (the marker) and the disk write are server-only.
	if (!Owner->HasAuthority())
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[Checkpoint] RecordCheckpoint ignored on non-authority for '%s'."), *GetNameSafe(Owner));
		return false;
	}

	UDP_SaveGameSubsystem* SaveSubsystem = ResolveSaveSubsystem();
	if (!SaveSubsystem)
	{
		// Inert default: no save backend -> we cannot persist a checkpoint. Log and bail without mutating state.
		UE_LOG(LogDPSave, Warning,
			TEXT("[Checkpoint] No save subsystem available; checkpoint for '%s' not recorded."), *GetNameSafe(Owner));
		return false;
	}

	// Update the replicated marker first so feedback fires even if the async disk write lags.
	LastCheckpointId = CheckpointId;
	LastCheckpointTransform = Owner->GetActorTransform();
	bHasCheckpoint = true;

	// Build a fresh save object capturing the checkpoint payload and hand it to the wrapped subsystem. The
	// subsystem owns all byte/header/IO machinery; we do not reinvent serialization here.
	UDP_SaveGame* SaveObject = BuildCheckpointSaveObject();
	const FString SlotName = GetCheckpointSlotName();

	bool bStarted = false;
	if (SaveObject && !SlotName.IsEmpty())
	{
		TWeakObjectPtr<USaveX_CheckpointComponent> WeakThis(this);
		const FGameplayTag RecordedId = CheckpointId;

		FDP_SaveCallbackDynamic OnComplete;
		OnComplete.BindWeakLambda(this, [WeakThis, RecordedId](FString /*Slot*/, EDP_SaveResult Result)
		{
			if (USaveX_CheckpointComponent* Self = WeakThis.Get())
			{
				const bool bOk = (Result == EDP_SaveResult::Success);
				Self->OnCheckpointRecorded.Broadcast(RecordedId, bOk);
				UE_CLOG(!bOk, LogDPSave, Warning,
					TEXT("[Checkpoint] Checkpoint slot write failed (result=%d)."), static_cast<int32>(Result));
			}
		});

		SaveSubsystem->SaveAsync(SlotName, SaveObject, OnComplete);
		bStarted = true;
	}
	else
	{
		// We could not construct a save object / had no slot; still surface the local "recorded" marker event
		// so HUD feedback works, but report failure.
		OnCheckpointRecorded.Broadcast(CheckpointId, /*bSuccess=*/false);
	}

	// A checkpoint is a strong "good place to save" signal: optionally kick the rotating autosave ring.
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const bool bAlsoAutosave = Settings ? Settings->bAutosaveOnCheckpoint : true /*defensive fallback*/;
	if (bAlsoAutosave)
	{
		if (USaveX_AutosaveSubsystem* Autosave = ResolveAutosaveSubsystem())
		{
			Autosave->RequestAutosave(ESaveX_AutosaveReason::Checkpoint);
		}
	}

	UE_LOG(LogDPSave, Log, TEXT("[Checkpoint] Recorded checkpoint '%s' for '%s' (writeStarted=%s)."),
		*CheckpointId.ToString(), *GetNameSafe(Owner), bStarted ? TEXT("yes") : TEXT("no"));
	return bStarted;
}

bool USaveX_CheckpointComponent::RestoreFromCheckpoint()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// AUTHORITY GUARD: restoring mutates authoritative actor state (transform) and loads from disk.
	if (!Owner->HasAuthority())
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[Checkpoint] RestoreFromCheckpoint ignored on non-authority for '%s'."), *GetNameSafe(Owner));
		return false;
	}

	UDP_SaveGameSubsystem* SaveSubsystem = ResolveSaveSubsystem();
	if (!SaveSubsystem)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[Checkpoint] No save subsystem available; cannot restore '%s'."), *GetNameSafe(Owner));
		OnCheckpointRestored.Broadcast(false);
		return false;
	}

	const FString SlotName = GetCheckpointSlotName();
	if (SlotName.IsEmpty() || !SaveSubsystem->DoesSlotExist(SlotName))
	{
		// No checkpoint persisted yet. Caller should fall back to a normal respawn.
		UE_LOG(LogDPSave, Log,
			TEXT("[Checkpoint] No checkpoint slot to restore for '%s'."), *GetNameSafe(Owner));
		OnCheckpointRestored.Broadcast(false);
		return false;
	}

	TWeakObjectPtr<USaveX_CheckpointComponent> WeakThis(this);

	FDP_LoadCallbackDynamic OnComplete;
	OnComplete.BindWeakLambda(this, [WeakThis](FString /*Slot*/, EDP_SaveResult Result, UDP_SaveGame* /*Loaded*/)
	{
		USaveX_CheckpointComponent* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		AActor* OwnerActor = Self->GetOwner();
		if (!OwnerActor || !OwnerActor->HasAuthority())
		{
			// Authority may have changed during the async load; do not mutate state off-authority.
			Self->OnCheckpointRestored.Broadcast(false);
			return;
		}

		const bool bOk = (Result == EDP_SaveResult::Success);
		if (bOk)
		{
			// Snap the owner back to the recorded checkpoint transform. The replicated marker already holds
			// the authoritative transform; persistable subobjects restore their own state via their own
			// authority-guarded RestoreState contract, which OnPostLoad on the save object drives.
			OwnerActor->SetActorTransform(Self->LastCheckpointTransform, /*bSweep=*/false,
				/*OutHit=*/nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			UE_LOG(LogDPSave, Warning,
				TEXT("[Checkpoint] Restore load failed (result=%d) for '%s'."),
				static_cast<int32>(Result), *GetNameSafe(OwnerActor));
		}
		Self->OnCheckpointRestored.Broadcast(bOk);
	});

	SaveSubsystem->LoadAsync(SlotName, OnComplete);

	UE_LOG(LogDPSave, Log, TEXT("[Checkpoint] Restore started for '%s' from slot '%s'."),
		*GetNameSafe(Owner), *SlotName);
	return true;
}

void USaveX_CheckpointComponent::OnRep_CheckpointId()
{
	// A new checkpoint marker arrived on a simulated proxy: surface local feedback (HUD/SFX). bHasCheckpoint
	// and the transform replicate alongside; we report success because the authority only replicates a
	// committed marker.
	if (bHasCheckpoint)
	{
		OnCheckpointRecorded.Broadcast(LastCheckpointId, /*bSuccess=*/true);
	}
}

TScriptInterface<ISeam_SaveSlotManager> USaveX_CheckpointComponent::ResolveSlotManager() const
{
	TScriptInterface<ISeam_SaveSlotManager> Result;

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Result;
	}

	// Prefer the project-configured service key; fall back to the conventional SaveSystem key.
	FGameplayTag Key;
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		Key = Settings->SlotManagerServiceTag;
	}
	if (!Key.IsValid())
	{
		Key = SaveX_ServiceKeys::SlotManager();
	}
	if (!Key.IsValid())
	{
		return Result;
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_SaveSlotManager::StaticClass()))
	{
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ISeam_SaveSlotManager>(Provider));
	}
	return Result;
}

UDP_SaveGameSubsystem* USaveX_CheckpointComponent::ResolveSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
}

USaveX_AutosaveSubsystem* USaveX_CheckpointComponent::ResolveAutosaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<USaveX_AutosaveSubsystem>(this);
}

FString USaveX_CheckpointComponent::GetCheckpointSlotName() const
{
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		if (!Settings->CheckpointSlotName.IsEmpty())
		{
			return Settings->CheckpointSlotName;
		}
	}
	// Defensive fallback so a null/blank CDO never produces an empty slot name (which would silently no-op).
	return TEXT("DPCheckpoint");
}

UDP_SaveGame* USaveX_CheckpointComponent::BuildCheckpointSaveObject() const
{
	// Choose the configured save class (a project subclass that gathers its own world state in OnPreSave),
	// falling back to the base UDP_SaveGame which still records header metadata.
	TSubclassOf<UDP_SaveGame> SaveClass = UDP_SaveGame::StaticClass();
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		if (UClass* Resolved = Settings->SaveGameClass.LoadSynchronous())
		{
			SaveClass = Resolved;
		}
	}

	UDP_SaveGame* SaveObject = NewObject<UDP_SaveGame>(GetTransientPackage(), SaveClass);
	if (SaveObject)
	{
		// Stamp human-facing header metadata. The reserved checkpoint slot is overwritten each time, so the
		// display name is stable.
		SaveObject->DisplayName = TEXT("Checkpoint");
	}
	return SaveObject;
}
