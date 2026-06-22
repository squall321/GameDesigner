// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Travel/Flow_TravelCoordinator.h"
#include "Flow/Travel/Flow_CarryOverSaveGame.h"
#include "Flow/Flow_GameFlowSubsystem.h"
#include "Flow/Flow_OrchestratorTypes.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Save/DPSaveGameSubsystem.h"
#include "Save/DPSaveGame.h"

#include "Persist/Seam_Persistable.h"

#include "Engine/Engine.h"
#include "Engine/World.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_TravelCoordinator::Initialize(UFlow_GameFlowSubsystem* InOwner)
{
	Owner = InOwner;

	// Wrap the engine's post-load + failure delegates (we own these registrations; removed in Shutdown).
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFlow_TravelCoordinator::HandlePostLoadMap);

	if (GEngine)
	{
		TravelFailureHandle  = GEngine->OnTravelFailure().AddUObject(this, &UFlow_TravelCoordinator::HandleTravelFailure);
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UFlow_TravelCoordinator::HandleNetworkFailure);
	}
}

void UFlow_TravelCoordinator::Shutdown()
{
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}
	if (GEngine)
	{
		if (TravelFailureHandle.IsValid())
		{
			GEngine->OnTravelFailure().Remove(TravelFailureHandle);
			TravelFailureHandle.Reset();
		}
		if (NetworkFailureHandle.IsValid())
		{
			GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
			NetworkFailureHandle.Reset();
		}
	}

	Participants.Reset();
	Owner.Reset();
}

// ---------------------------------------------------------------------------------------------------
// Participant registration
// ---------------------------------------------------------------------------------------------------

void UFlow_TravelCoordinator::RegisterCarryOverParticipant(UObject* Participant)
{
	if (!Participant || !Participant->Implements<USeam_Persistable>())
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][Travel] RegisterCarryOverParticipant: object does not implement ISeam_Persistable."));
		return;
	}
	PruneParticipants();
	Participants.AddUnique(Participant);
}

void UFlow_TravelCoordinator::UnregisterCarryOverParticipant(UObject* Participant)
{
	Participants.RemoveAll([Participant](const TWeakObjectPtr<UObject>& W) { return W.Get() == Participant; });
}

void UFlow_TravelCoordinator::PruneParticipants()
{
	Participants.RemoveAll([](const TWeakObjectPtr<UObject>& W) { return !W.IsValid(); });
}

// ---------------------------------------------------------------------------------------------------
// Pre-travel capture
// ---------------------------------------------------------------------------------------------------

void UFlow_TravelCoordinator::PrepareTravel(FGameplayTag TargetPhase, bool bSeamless)
{
	PendingTargetPhase = TargetPhase;

	UFlow_CarryOverSaveGame* Carry = CaptureCarryOver();
	if (Carry)
	{
		Carry->TargetPhase = TargetPhase;

		const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
		const FString Slot = (Settings && !Settings->CarryOverSlotName.IsEmpty())
			? Settings->CarryOverSlotName
			: TEXT("_dp_carryover");

		if (UDP_SaveGameSubsystem* Save = GetSaveSubsystem())
		{
			// Synchronous write: travel may begin on the very next frame, so the bytes must be on disk
			// before the world is torn down. The blob is small (carry-over records only).
			const EDP_SaveResult Result = Save->SaveNow(Slot, Carry);
			if (Result == EDP_SaveResult::Success)
			{
				bRestorePending = true;
				UE_LOG(LogDP, Log, TEXT("[Flow][Travel] Wrote %d carry-over record(s) to slot '%s'."),
					Carry->Records.Num(), *Slot);
			}
			else
			{
				UE_LOG(LogDP, Warning, TEXT("[Flow][Travel] Carry-over write to '%s' failed (result %d)."),
					*Slot, static_cast<int32>(Result));
			}
		}
	}

	// Announce the travel start regardless of carry-over success.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		FFlow_TravelPayload Payload;
		Payload.TargetPhase = TargetPhase;
		Payload.bFailed = false;
		Payload.bSeamless = bSeamless;
		Bus->BroadcastPayload(FlowTags::Bus_TravelStarted, FInstancedStruct::Make(Payload), this);
	}
}

UFlow_CarryOverSaveGame* UFlow_TravelCoordinator::CaptureCarryOver()
{
	PruneParticipants();
	if (Participants.Num() == 0)
	{
		return nullptr;
	}

	UFlow_CarryOverSaveGame* Carry = NewObject<UFlow_CarryOverSaveGame>(this);
	for (const TWeakObjectPtr<UObject>& Weak : Participants)
	{
		UObject* P = Weak.Get();
		if (!P || !P->Implements<USeam_Persistable>())
		{
			continue;
		}

		const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(P);
		if (!Kind.IsValid())
		{
			continue;
		}

		FInstancedStruct State;
		ISeam_Persistable::Execute_CaptureState(P, State);
		if (State.IsValid())
		{
			Carry->SetRecord(Kind, State);
		}
	}
	return Carry;
}

// ---------------------------------------------------------------------------------------------------
// Post-travel restore
// ---------------------------------------------------------------------------------------------------

void UFlow_TravelCoordinator::HandlePostLoadMap(UWorld* /*LoadedWorld*/)
{
	if (bRestorePending)
	{
		RestoreCarryOver();
	}
}

void UFlow_TravelCoordinator::RestoreCarryOver()
{
	bRestorePending = false;

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const FString Slot = (Settings && !Settings->CarryOverSlotName.IsEmpty())
		? Settings->CarryOverSlotName
		: TEXT("_dp_carryover");

	UDP_SaveGameSubsystem* Save = GetSaveSubsystem();
	if (!Save || !Save->DoesSlotExist(Slot))
	{
		return;
	}

	EDP_SaveResult Result = EDP_SaveResult::Success;
	UDP_SaveGame* Loaded = Save->LoadNow(Slot, Result);
	UFlow_CarryOverSaveGame* Carry = Cast<UFlow_CarryOverSaveGame>(Loaded);
	if (Result != EDP_SaveResult::Success || !Carry)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][Travel] Carry-over load from '%s' failed (result %d)."),
			*Slot, static_cast<int32>(Result));
		return;
	}

	// Scatter each record to live participants whose kind matches. Restore is authority-guarded inside the
	// participant, so a client-side restore is a documented no-op.
	PruneParticipants();
	int32 Restored = 0;
	for (const TWeakObjectPtr<UObject>& Weak : Participants)
	{
		UObject* P = Weak.Get();
		if (!P || !P->Implements<USeam_Persistable>())
		{
			continue;
		}
		const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(P);
		if (const FFlow_CarryOverRecord* Record = Carry->FindRecord(Kind))
		{
			ISeam_Persistable::Execute_RestoreState(P, Record->State);
			++Restored;
		}
	}

	UE_LOG(LogDP, Log, TEXT("[Flow][Travel] Restored %d carry-over record(s) from slot '%s'."), Restored, *Slot);

	// If the carry-over slot is transient, delete it after a successful restore so a stale carry-over can
	// never be re-applied on a later, unrelated travel.
	if (Settings && !Settings->bWriteCarryOverToProfile)
	{
		Save->DeleteSlot(Slot);
	}
}

// ---------------------------------------------------------------------------------------------------
// Failure recovery
// ---------------------------------------------------------------------------------------------------

void UFlow_TravelCoordinator::HandleTravelFailure(UWorld* /*World*/, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	const FString Reason = FString::Printf(TEXT("TravelFailure(%s): %s"),
		ETravelFailure::ToString(FailureType), *ErrorString);
	EnterTravelError(Reason);
}

void UFlow_TravelCoordinator::HandleNetworkFailure(UWorld* /*World*/, UNetDriver* /*NetDriver*/, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	const FString Reason = FString::Printf(TEXT("NetworkFailure(%s): %s"),
		ENetworkFailure::ToString(FailureType), *ErrorString);
	EnterTravelError(Reason);
}

void UFlow_TravelCoordinator::EnterTravelError(const FString& Reason)
{
	UE_LOG(LogDP, Error, TEXT("[Flow][Travel] %s -> NetError recovery."), *Reason);

	// A failed travel invalidates the pending restore (the destination never came up).
	bRestorePending = false;

	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		FFlow_TravelPayload Payload;
		Payload.TargetPhase = PendingTargetPhase;
		Payload.bFailed = true;
		Payload.Detail = Reason;
		Bus->BroadcastPayload(FlowTags::Bus_TravelFailed, FInstancedStruct::Make(Payload), this);
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const FGameplayTag ErrorPhase = (Settings && Settings->NetErrorPhase.IsValid())
		? Settings->NetErrorPhase
		: FlowTags::Phase_NetError;

	if (UFlow_GameFlowSubsystem* Flow = Owner.Get())
	{
		Flow->ForceTransition(ErrorPhase);
	}
}

// ---------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------

UDP_SaveGameSubsystem* UFlow_TravelCoordinator::GetSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this);
}

UDP_MessageBusSubsystem* UFlow_TravelCoordinator::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}
