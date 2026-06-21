// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/Narr_SaveGame.h"
#include "Story/Narr_StoryDirectorSubsystem.h"
#include "Reputation/Narr_ReputationSubsystem.h"

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Persist/Seam_Persistable.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"

void UNarr_SaveGame::RegisterParticipant(const TScriptInterface<ISeam_Persistable>& Participant)
{
	if (Participant.GetObject() && Participant.GetInterface())
	{
		ExtraParticipants.Add(TWeakInterfacePtr<ISeam_Persistable>(Participant.GetInterface()));
	}
}

UObject* UNarr_SaveGame::ResolveContext() const
{
	if (UObject* Ctx = SaveContext.Get())
	{
		return Ctx;
	}
	// Fall back to the outer chain so a save built with no explicit context still resolves subsystems.
	return GetOuter();
}

void UNarr_SaveGame::GatherBuiltinParticipants(TArray<TScriptInterface<ISeam_Persistable>>& Out) const
{
	UObject* Ctx = ResolveContext();
	if (!Ctx)
	{
		return;
	}

	// The story director is the canonical always-saved narrative participant.
	if (UNarr_StoryDirectorSubsystem* Director =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UNarr_StoryDirectorSubsystem>(Ctx))
	{
		TScriptInterface<ISeam_Persistable> Iface;
		Iface.SetObject(Director);
		Iface.SetInterface(Cast<ISeam_Persistable>(Director));
		if (Iface.GetInterface())
		{
			Out.Add(Iface);
		}
	}

	// The reputation subsystem is the second always-saved narrative participant (the reputation owner).
	if (UNarr_ReputationSubsystem* Reputation =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UNarr_ReputationSubsystem>(Ctx))
	{
		TScriptInterface<ISeam_Persistable> Iface;
		Iface.SetObject(Reputation);
		Iface.SetInterface(Cast<ISeam_Persistable>(Reputation));
		if (Iface.GetInterface())
		{
			Out.Add(Iface);
		}
	}
}

void UNarr_SaveGame::CaptureParticipant(const TScriptInterface<ISeam_Persistable>& Participant)
{
	UObject* Obj = Participant.GetObject();
	if (!Obj || !Participant.GetInterface())
	{
		return;
	}

	// ISeam_Persistable methods are BlueprintNativeEvent — invoke through the Execute_ thunks.
	const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Obj);
	if (!Kind.IsValid())
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Narr] Skipping participant '%s' with no persistence kind."), *Obj->GetName());
		return;
	}

	FInstancedStruct Payload;
	ISeam_Persistable::Execute_CaptureState(Obj, Payload);
	if (Payload.IsValid())
	{
		Participants.Emplace(Kind, Payload);
		UE_LOG(LogDPSave, Verbose, TEXT("[Narr] Captured narrative participant kind '%s'."), *Kind.ToString());
	}
}

void UNarr_SaveGame::OnPreSave_Implementation()
{
	Super::OnPreSave_Implementation();

	Participants.Reset();

	// Collect builtin + explicitly-registered participants, de-duplicated by object identity so a
	// participant registered AND discovered is captured once.
	TArray<TScriptInterface<ISeam_Persistable>> ToCapture;
	GatherBuiltinParticipants(ToCapture);

	TSet<const UObject*> Seen;
	for (const TScriptInterface<ISeam_Persistable>& P : ToCapture)
	{
		Seen.Add(P.GetObject());
	}

	for (const TWeakInterfacePtr<ISeam_Persistable>& Weak : ExtraParticipants)
	{
		if (UObject* Obj = Weak.GetObject())
		{
			if (!Seen.Contains(Obj))
			{
				TScriptInterface<ISeam_Persistable> Iface;
				Iface.SetObject(Obj);
				Iface.SetInterface(Weak.Get());
				ToCapture.Add(Iface);
				Seen.Add(Obj);
			}
		}
	}

	for (const TScriptInterface<ISeam_Persistable>& P : ToCapture)
	{
		CaptureParticipant(P);
	}

	UE_LOG(LogDPSave, Log, TEXT("[Narr] Save gathered %d narrative participant record(s)."), Participants.Num());
}

void UNarr_SaveGame::RestoreToKind(const FGameplayTag& Kind, const FInstancedStruct& Payload)
{
	UObject* Ctx = ResolveContext();
	if (!Ctx || !Kind.IsValid())
	{
		return;
	}

	// Builtin: the story director.
	if (UNarr_StoryDirectorSubsystem* Director =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UNarr_StoryDirectorSubsystem>(Ctx))
	{
		if (ISeam_Persistable::Execute_GetPersistenceKind(Director) == Kind)
		{
			// RestoreState is authority-guarded by the participant itself (no-op on clients).
			ISeam_Persistable::Execute_RestoreState(Director, Payload);
			return;
		}
	}

	// Explicitly-registered participants.
	for (const TWeakInterfacePtr<ISeam_Persistable>& Weak : ExtraParticipants)
	{
		if (UObject* Obj = Weak.GetObject())
		{
			if (ISeam_Persistable::Execute_GetPersistenceKind(Obj) == Kind)
			{
				ISeam_Persistable::Execute_RestoreState(Obj, Payload);
				return;
			}
		}
	}

	UE_LOG(LogDPSave, Verbose, TEXT("[Narr] No live participant matched restore kind '%s'."), *Kind.ToString());
}

void UNarr_SaveGame::OnPostLoad_Implementation()
{
	Super::OnPostLoad_Implementation();

	for (const FNarr_PersistedParticipant& Record : Participants)
	{
		RestoreToKind(Record.Kind, Record.Payload);
	}

	UE_LOG(LogDPSave, Log, TEXT("[Narr] Save scattered %d narrative participant record(s)."), Participants.Num());
}
