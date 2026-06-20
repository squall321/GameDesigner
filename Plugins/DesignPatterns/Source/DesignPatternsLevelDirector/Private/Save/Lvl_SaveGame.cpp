// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/Lvl_SaveGame.h"
#include "Placement/Lvl_ProceduralPlacerComponent.h"
#include "DesignPatternsLevelDirectorNativeTags.h"

#include "Core/DPLog.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

void ULvl_SaveGame::CollectPlacers(const UObject* WorldContextObject,
	TArray<ULvl_ProceduralPlacerComponent*>& OutPlacers)
{
	OutPlacers.Reset();
	if (!GEngine || !WorldContextObject)
	{
		return;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return;
	}

	// Iterate every actor in the world and gather its placer components. TActorIterator skips the
	// CDO/transient template actors, so this only sees live, level-placed/spawned actors.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		TArray<ULvl_ProceduralPlacerComponent*> Components;
		Actor->GetComponents<ULvl_ProceduralPlacerComponent>(Components);
		OutPlacers.Append(Components);
	}
}

int32 ULvl_SaveGame::CaptureFromWorld(const UObject* WorldContextObject)
{
	Manifests.Reset();

	TArray<ULvl_ProceduralPlacerComponent*> Placers;
	CollectPlacers(WorldContextObject, Placers);

	for (ULvl_ProceduralPlacerComponent* Placer : Placers)
	{
		if (!Placer)
		{
			continue;
		}
		const FLvl_PlacementManifest& Manifest = Placer->GetManifest();
		// Only persist passes that actually placed something — an empty manifest is reproduced by
		// simply re-running GeneratePlacement on load, so it adds no value to the save.
		if (Manifest.HasEntries())
		{
			Manifests.Add(Manifest);
		}
	}

	UE_LOG(LogDP, Log, TEXT("Lvl SaveGame: captured %d placement manifest(s)."), Manifests.Num());
	return Manifests.Num();
}

int32 ULvl_SaveGame::RestoreInto(const UObject* WorldContextObject)
{
	if (Manifests.Num() == 0)
	{
		return 0;
	}

	TArray<ULvl_ProceduralPlacerComponent*> Placers;
	CollectPlacers(WorldContextObject, Placers);
	if (Placers.Num() == 0)
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl SaveGame: RestoreInto found no placers in the world."));
		return 0;
	}

	int32 TotalRespawned = 0;

	// Route each stored manifest back to the placer it came from. Matching is by (RuleSetTag, RegionTag)
	// — the stable identity the manifest records. A placer whose pair matches restores that manifest;
	// each manifest is consumed once so two placers sharing identity do not double-spawn.
	TArray<bool> Consumed;
	Consumed.Init(false, Manifests.Num());

	for (ULvl_ProceduralPlacerComponent* Placer : Placers)
	{
		if (!Placer)
		{
			continue;
		}

		const FGameplayTag PlacerRegion = Placer->GetEffectiveRegionTag();
		const FGameplayTag PlacerRuleSet = Placer->RuleSet ? Placer->RuleSet->DataTag : FGameplayTag();

		for (int32 Index = 0; Index < Manifests.Num(); ++Index)
		{
			if (Consumed[Index])
			{
				continue;
			}
			const FLvl_PlacementManifest& Manifest = Manifests[Index];
			const bool bRegionMatches = (Manifest.RegionTag == PlacerRegion);
			const bool bRuleSetMatches = (!Manifest.RuleSetTag.IsValid() || Manifest.RuleSetTag == PlacerRuleSet);
			if (bRegionMatches && bRuleSetMatches)
			{
				// RestoreFromManifest is authority-guarded internally: a client-side call is a no-op.
				TotalRespawned += Placer->RestoreFromManifest(Manifest);
				Consumed[Index] = true;
				break;
			}
		}
	}

	UE_LOG(LogDP, Log, TEXT("Lvl SaveGame: restored %d actor(s) across %d manifest(s)."),
		TotalRespawned, Manifests.Num());
	return TotalRespawned;
}

//~ UDP_SaveGame hooks ------------------------------------------------------------------------------

void ULvl_SaveGame::OnPreSave_Implementation()
{
	if (const UObject* Context = SaveWorldContext.Get())
	{
		CaptureFromWorld(Context);
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl SaveGame: OnPreSave with no world context set; skipping capture."));
	}
}

void ULvl_SaveGame::OnPostLoad_Implementation()
{
	if (const UObject* Context = SaveWorldContext.Get())
	{
		RestoreInto(Context);
	}
	else
	{
		UE_LOG(LogDP, Verbose, TEXT("Lvl SaveGame: OnPostLoad with no world context set; skipping restore."));
	}
}

//~ ISeam_Persistable -------------------------------------------------------------------------------

void ULvl_SaveGame::CaptureState_Implementation(FInstancedStruct& Out) const
{
	// Pack the whole combined manifest list into a single placement record. (Per-placer participation
	// would pack one manifest; this save object owns the aggregate.)
	FLvl_PlacementSaveRecord Record;
	if (Manifests.Num() > 0)
	{
		// The aggregate record carries the FIRST manifest's identity at the top level for routing; the
		// remaining manifests ride along in the save object's Manifests array, which is the canonical
		// store. When only one manifest exists this collapses to a clean single record.
		Record.Manifest = Manifests[0];
	}
	Out = FInstancedStruct::Make(Record);
}

void ULvl_SaveGame::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (const FLvl_PlacementSaveRecord* Record = In.GetPtr<FLvl_PlacementSaveRecord>())
	{
		// Merge the record's manifest in if it is not already represented (idempotent restore).
		const bool bAlreadyPresent = Manifests.ContainsByPredicate(
			[&Record](const FLvl_PlacementManifest& M)
			{
				return M.RuleSetTag == Record->Manifest.RuleSetTag && M.RegionTag == Record->Manifest.RegionTag;
			});
		if (!bAlreadyPresent && Record->Manifest.HasEntries())
		{
			Manifests.Add(Record->Manifest);
		}
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("Lvl SaveGame: RestoreState received a non-placement record; ignored."));
	}
}

FGameplayTag ULvl_SaveGame::GetPersistenceKind_Implementation() const
{
	return LvlNativeTags::Persist_Lvl_Placement;
}
