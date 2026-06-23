// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Persist/Seam_Persistable.h"
#include "Memory/SimAg_MemoryArray.h"
#include "SimAg_MemoryComponent.generated.h"

class USimAg_ClockSubsystem;

/** Fired (server and clients) after the memory store changes (a fact remembered, decayed, or pruned). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnMemoryChanged, USimAg_MemoryComponent*, MemoryComponent);

/**
 * One persisted memory fact. Mirrors the replicated FSimAg_MemoryFact in a plain SaveGame struct (saves
 * are local-only; the net path uses the fast array instead).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_SavedMemoryFact
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGameplayTag Subject;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId Entity;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	float Confidence = 1.f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	double LastSeenDays = 0.0;
};

/**
 * An agent memory component's CaptureState record: the owning agent's id plus its remembered facts. The
 * concrete type USimAg_MemoryComponent writes into the ISeam_Persistable FInstancedStruct out-parameter.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MemoryRecord
{
	GENERATED_BODY()

	/** Stable id of the agent that owns this memory, so a restore routes the record to the right pawn. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId AgentId;

	/** Every remembered fact at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	TArray<FSimAg_SavedMemoryFact> Facts;
};

/**
 * Replicated decaying-knowledge store for one agent. Remembers tagged facts (seen resources, threats,
 * other agents' last-known positions) with a confidence that ages on the simulation clock, feeding the
 * brain's knowledge / social / haul strategies.
 *
 * AUTHORITY & REPLICATION: the server ages confidence every frame (scaled by the sim clock, reusing the
 * USimAg_NeedsComponent clock pattern), prunes facts below MemoryPruneConfidence, and flushes the
 * replicated fast array on a THROTTLED cadence (MemoryReplicationCadence) so a crowd costs bounded
 * bandwidth. Every mutator (RememberFact, ForgetSubject) guards authority at the TOP and early-returns on
 * clients; clients only observe replicated facts. Save participant via ISeam_Persistable (Persist_Memory).
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_MemoryComponent
	: public UActorComponent
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	USimAg_MemoryComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	/**
	 * Record (or refresh) a fact. AUTHORITY ONLY: early-returns on clients. If a fact with the same
	 * Subject AND Entity already exists it is refreshed in place (location/confidence/time updated);
	 * otherwise a new fact is added. LastSeenDays is stamped to the current sim day if the caller left it
	 * at 0 (so callers usually just fill Subject/WorldLocation/Confidence).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Memory")
	void RememberFact(const FSimAg_MemoryFact& Fact);

	/** Forget every fact about Subject. AUTHORITY ONLY. @return number of facts removed. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Memory")
	int32 ForgetSubject(FGameplayTag Subject);

	/**
	 * Find the nearest remembered fact of Subject to From whose DECAYED confidence is still above
	 * MemoryPruneConfidence. Client-safe (reads replicated facts). @return true and fills Out when found.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Memory")
	bool QueryNearest(FGameplayTag Subject, const FVector& From, FSimAg_MemoryFact& Out) const;

	/**
	 * Strongest (highest decayed-confidence) remembered fact of Subject regardless of distance.
	 * Client-safe. @return true and fills Out when any non-pruned fact of Subject exists.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Memory")
	bool QueryStrongest(FGameplayTag Subject, FSimAg_MemoryFact& Out) const;

	/** Read-only snapshot of all facts (safe on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Memory")
	TArray<FSimAg_MemoryFact> GetFacts() const { return Memory.Facts; }

	/** Decayed confidence of the strongest fact about Subject right now, or 0 if none. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Memory")
	float GetDecayedConfidenceFor(FGameplayTag Subject) const;

	/** Fired after the memory store changes (server aging / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Memory")
	FSimAg_OnMemoryChanged OnMemoryChanged;

	/** Called by the fast-array callbacks on clients to surface a replicated memory change. */
	void HandleReplicatedChange();

	/** Current simulation time in fractional DAYS (clock day + time-of-day). 0 if no clock. Public so
	 *  strategies can age facts consistently with the component. */
	double GetNowDays() const;

protected:
	/**
	 * Facts this component starts with (authored design-time). Copied into the replicated array on
	 * BeginPlay (authority only), with LastSeenDays stamped to the start day.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory")
	TArray<FSimAg_MemoryFact> DefaultFacts;

private:
	/** Replicated facts (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_MemoryArray Memory;

	/** Real-time accumulator since the last replication flush, compared against the settings cadence. */
	float ReplicationAccumulator = 0.f;

	/** Cached replication cadence (seconds) from settings, refreshed at BeginPlay. */
	float ReplicationCadence = 1.f;

	/** Cached half-life (sim days) from settings. */
	float HalfLifeDays = 1.f;

	/** Cached prune threshold (decayed confidence) from settings. */
	float PruneConfidence = 0.05f;

	/** Weak, non-owning handle to the world clock; resolved lazily and null-checked before deref. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_ClockSubsystem> CachedClock;

	/** Resolve (and cache) the world clock subsystem, preferring the service-locator key. Null-safe. */
	USimAg_ClockSubsystem* GetClock() const;

	/** Server: prune facts whose decayed confidence has fallen below PruneConfidence. */
	void PruneFaded();

	/** Find a fact matching Subject+Entity (mutable). Null if absent. */
	FSimAg_MemoryFact* FindFact(const FGameplayTag& Subject, const FSeam_EntityId& Entity);

	/** Stable id of the owning agent (resolved off a sibling agent component); invalid if none. */
	FSeam_EntityId ResolveAgentId() const;
};
