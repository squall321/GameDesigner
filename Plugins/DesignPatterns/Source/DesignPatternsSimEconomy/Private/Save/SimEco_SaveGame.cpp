// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/SimEco_SaveGame.h"
#include "Market/SimEco_EconomyTags.h"
#include "Persist/Seam_Persistable.h"
#include "Core/DPLog.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"

UWorld* USimEco_SaveGame::ResolveWorld() const
{
	if (!GEngine || !TargetWorldContext)
	{
		return nullptr;
	}
	return GEngine->GetWorldFromContextObject(TargetWorldContext, EGetWorldErrorMode::ReturnNull);
}

FString USimEco_SaveGame::MakeInstanceKey(const UObject* Object)
{
	return Object ? Object->GetPathName() : FString();
}

void USimEco_SaveGame::CollectParticipants(UWorld* World, TArray<TWeakObjectPtr<UObject>>& OutObjects) const
{
	OutObjects.Reset();
	if (!World)
	{
		return;
	}

	const FGameplayTag PersistRoot = SimEcoEconomyTags::Persist;

	// Helper: accept an object if it implements the persist seam and its kind is under SimEco.Persist.
	auto ConsiderObject = [&OutObjects, &PersistRoot](UObject* Candidate)
	{
		if (!Candidate || !Candidate->GetClass()->ImplementsInterface(USeam_Persistable::StaticClass()))
		{
			return;
		}
		const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Candidate);
		if (Kind.IsValid() && Kind.MatchesTag(PersistRoot))
		{
			OutObjects.AddUnique(Candidate);
		}
	};

	// 1) World subsystems (the market subsystem is persistable).
	for (UWorldSubsystem* Subsystem : World->GetSubsystemArray<UWorldSubsystem>())
	{
		ConsiderObject(Subsystem);
	}

	// 2) Actors and their components (stockpiles etc.).
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		ConsiderObject(Actor);
		for (UActorComponent* Component : Actor->GetComponents())
		{
			ConsiderObject(Component);
		}
	}
}

void USimEco_SaveGame::OnPreSave_Implementation()
{
	Super::OnPreSave_Implementation();

	Participants.Reset();

	UWorld* World = ResolveWorld();
	if (!World)
	{
		UE_LOG(LogDP, Warning, TEXT("SimEco save: no TargetWorldContext set; nothing captured."));
		return;
	}

	TArray<TWeakObjectPtr<UObject>> Objects;
	CollectParticipants(World, Objects);

	for (const TWeakObjectPtr<UObject>& WeakObj : Objects)
	{
		UObject* Obj = WeakObj.Get();
		if (!Obj)
		{
			continue;
		}

		FInstancedStruct State;
		ISeam_Persistable::Execute_CaptureState(Obj, State);
		if (!State.IsValid())
		{
			continue; // participant had nothing to save
		}

		FSimEco_SavedParticipant& Record = Participants.AddDefaulted_GetRef();
		Record.Kind = ISeam_Persistable::Execute_GetPersistenceKind(Obj);
		Record.InstanceKey = MakeInstanceKey(Obj);
		Record.State = MoveTemp(State);
	}

	UE_LOG(LogDP, Log, TEXT("SimEco save captured %d participant(s)."), Participants.Num());
}

void USimEco_SaveGame::OnPostLoad_Implementation()
{
	Super::OnPostLoad_Implementation();

	UWorld* World = ResolveWorld();
	if (!World)
	{
		UE_LOG(LogDP, Warning, TEXT("SimEco load: no TargetWorldContext set; nothing restored."));
		return;
	}

	TArray<TWeakObjectPtr<UObject>> Objects;
	CollectParticipants(World, Objects);

	// Index live participants by (Kind, InstanceKey) for routing, with a kind-only fallback for
	// singletons whose path may have shifted between sessions (e.g. the market subsystem).
	TMap<FString, UObject*> ByKey;
	TMap<FGameplayTag, UObject*> SingletonByKind;
	for (const TWeakObjectPtr<UObject>& WeakObj : Objects)
	{
		UObject* Obj = WeakObj.Get();
		if (!Obj)
		{
			continue;
		}
		const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Obj);
		ByKey.Add(MakeInstanceKey(Obj), Obj);
		// First object of a kind wins the singleton slot.
		if (!SingletonByKind.Contains(Kind))
		{
			SingletonByKind.Add(Kind, Obj);
		}
	}

	int32 Restored = 0;
	for (const FSimEco_SavedParticipant& Record : Participants)
	{
		UObject* Target = nullptr;
		if (UObject** Found = ByKey.Find(Record.InstanceKey))
		{
			Target = *Found;
		}
		else if (UObject** FoundSingleton = SingletonByKind.Find(Record.Kind))
		{
			// Fall back to the kind's singleton when the exact instance key no longer resolves.
			Target = *FoundSingleton;
		}

		if (Target && Record.State.IsValid())
		{
			// RestoreState is itself authority-guarded by each participant; a client load is a no-op.
			ISeam_Persistable::Execute_RestoreState(Target, Record.State);
			++Restored;
		}
	}

	UE_LOG(LogDP, Log, TEXT("SimEco load restored %d of %d participant(s)."), Restored, Participants.Num());
}
