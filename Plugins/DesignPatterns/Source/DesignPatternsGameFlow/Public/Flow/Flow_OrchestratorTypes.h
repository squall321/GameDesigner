// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject on 5.5+. Included BEFORE
// the .generated.h, matching the module convention (the carry-over record holds an FInstancedStruct).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Flow_OrchestratorTypes.generated.h"

/**
 * One durable carry-over record: a single ISeam_Persistable participant's state, routed by its
 * GetPersistenceKind() tag. Lives ONLY in the carry-over save's byte stream (a save object is never
 * replicated), so the FInstancedStruct here is allowed by the module's <=5.4 StructUtils guard.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_CarryOverRecord
{
	GENERATED_BODY()

	/** The participant kind this record belongs to (from ISeam_Persistable::GetPersistenceKind). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Flow|Travel")
	FGameplayTag Kind;

	/** The captured participant state (each participant defines its own concrete record struct). */
	UPROPERTY(SaveGame)
	FInstancedStruct State;
};

/**
 * Bus payload announcing a matchmaking state change. Flat (tags + scalars, no object refs) so any UI /
 * analytics can read it off DP.Bus.Flow.MatchmakingChanged without depending on this module or the Net
 * module. Broadcast wrapped in FInstancedStruct, never plain-replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_MatchmakingPayload
{
	GENERATED_BODY()

	/** The matchmaking phase tag (mirrors the net-session phase, anchored under DP.Flow.Matchmaking.Phase.*). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Matchmaking")
	FGameplayTag Phase;

	/** Current retry attempt (0 = first attempt, increments on each backoff retry). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Matchmaking")
	int32 RetryAttempt = 0;

	/** True once matchmaking has terminally failed (retries exhausted / online unavailable). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Matchmaking")
	bool bTerminalFailure = false;

	/** Number of results found by the most recent search (valid after a search completes). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Matchmaking")
	int32 ResultCount = 0;
};

/**
 * Bus payload announcing a travel start or a travel failure. Flat and net/save-safe. Broadcast on
 * DP.Bus.Flow.TravelStarted (bFailed=false) and DP.Bus.Flow.TravelFailed (bFailed=true).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_TravelPayload
{
	GENERATED_BODY()

	/** The phase that triggered this travel. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Travel")
	FGameplayTag TargetPhase;

	/** True if this announces a failure rather than a start. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Travel")
	bool bFailed = false;

	/** True if seamless/relative travel was chosen (false = absolute/OpenLevel). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Travel")
	bool bSeamless = false;

	/** Diagnostic detail (failure reason text or destination map name). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Travel")
	FString Detail;
};

/**
 * Bus payload announcing a boot-sequence step change. Flat. Broadcast on DP.Bus.Flow.BootStepChanged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_BootStepPayload
{
	GENERATED_BODY()

	/** The kind tag of the step now active (Flow.BootStep.*). Invalid once the sequence completes. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Boot")
	FGameplayTag StepKind;

	/** Index of the active step in the sequence. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Boot")
	int32 StepIndex = 0;

	/** Total number of steps in the sequence. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Boot")
	int32 StepCount = 0;

	/** True once the whole boot sequence has completed (this is the terminal broadcast). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow|Boot")
	bool bComplete = false;
};
