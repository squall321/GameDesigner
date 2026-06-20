// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "SimEco_SaveGame.generated.h"

/**
 * One captured economy participant: its persistence-kind tag, an optional disambiguator (the entity
 * id / actor name for participants that have many instances of the same kind), and the opaque state
 * blob the participant produced via ISeam_Persistable::CaptureState.
 */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_SavedParticipant
{
	GENERATED_BODY()

	/** The participant's persistence-kind (e.g. SimEco.Persist.Market, SimEco.Persist.Stockpile). */
	UPROPERTY(SaveGame)
	FGameplayTag Kind;

	/**
	 * Per-instance key used to route a record back to the right participant when several share a Kind
	 * (e.g. many stockpiles). Empty for singletons like the market. Derived from the owning object's
	 * stable name/path.
	 */
	UPROPERTY(SaveGame)
	FString InstanceKey;

	/** Opaque participant state captured via ISeam_Persistable::CaptureState. */
	UPROPERTY(SaveGame)
	FInstancedStruct State;
};

/**
 * Save object for the simulation economy.
 *
 * On OnPreSave (game thread) it gathers every ISeam_Persistable economy participant in the world —
 * the market subsystem and any stockpile components/actors whose persistence kind is anchored under
 * SimEco.Persist — and stores their captured state. On OnPostLoad (game thread) it scatters records
 * back to the matching participants (each RestoreState is itself authority-guarded, so a client-side
 * load is a no-op).
 *
 * Set TargetWorldContext before SaveAsync/LoadAsync so the gather/scatter knows which world to walk.
 */
UCLASS(BlueprintType, Blueprintable)
class DESIGNPATTERNSSIMECONOMY_API USimEco_SaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	//~ Begin UDP_SaveGame
	/** Gather all economy participants' state on the game thread. */
	virtual void OnPreSave_Implementation() override;
	/** Scatter restored state back to matching participants on the game thread. */
	virtual void OnPostLoad_Implementation() override;
	//~ End UDP_SaveGame

	/**
	 * World-context object the gather/scatter walks. MUST be set (to any object in the target world)
	 * by the caller before save/load — the save object has no implicit world otherwise.
	 */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "SimEconomy|Save")
	TObjectPtr<UObject> TargetWorldContext = nullptr;

	/** Captured participant records (serialized). */
	UPROPERTY(SaveGame)
	TArray<FSimEco_SavedParticipant> Participants;

private:
	/** Resolve the world to walk from TargetWorldContext (null-safe). */
	UWorld* ResolveWorld() const;

	/**
	 * Collect every ISeam_Persistable in the world whose persistence kind sits under the SimEco
	 * persistence root, as (object, kind, instance-key) tuples on the game thread.
	 */
	void CollectParticipants(UWorld* World, TArray<TWeakObjectPtr<UObject>>& OutObjects) const;

	/** Build a stable per-instance key for an object (its full path name). */
	static FString MakeInstanceKey(const UObject* Object);
};
