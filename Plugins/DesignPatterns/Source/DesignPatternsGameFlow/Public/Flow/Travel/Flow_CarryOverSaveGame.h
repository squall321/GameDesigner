// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "Flow/Flow_OrchestratorTypes.h"
#include "Flow_CarryOverSaveGame.generated.h"

/**
 * Durable carry-over record round-tripped by UFlow_TravelCoordinator through the core
 * UDP_SaveGameSubsystem across a level travel. Holds a PLAIN UPROPERTY(SaveGame) array of carry-over
 * records — a save object is NEVER replicated, so the FInstancedStruct inside each record is legitimate
 * (it lives only in the save byte stream, covered by the module's <=5.4 StructUtils guard).
 *
 * Pre-travel: the coordinator gathers each ISeam_Persistable carry-over participant via CaptureState into
 * a record keyed by GetPersistenceKind(), then writes this object to the configured carry-over slot.
 * Post-travel: it loads the object back and scatters each record to the matching participant in the new
 * world via RestoreState (which is authority-guarded inside the participant). The record array survives
 * because the byte blob is identical across the travel boundary.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSGAMEFLOW_API UFlow_CarryOverSaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** The carry-over records, one per participant kind. Plain array — never a fast array (no replication). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Flow|Travel")
	TArray<FFlow_CarryOverRecord> Records;

	/** The phase the travel was heading to when this was captured (diagnostics / restore routing). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Flow|Travel")
	FGameplayTag TargetPhase;

	/** Find the record for Kind, or null. */
	const FFlow_CarryOverRecord* FindRecord(FGameplayTag Kind) const;

	/** Upsert a record for Kind with the given state (replaces any existing record of the same kind). */
	void SetRecord(FGameplayTag Kind, const FInstancedStruct& State);

	/** Clear all records (e.g. after a successful restore so a stale carry-over can't be re-applied). */
	void ClearRecords();
};
