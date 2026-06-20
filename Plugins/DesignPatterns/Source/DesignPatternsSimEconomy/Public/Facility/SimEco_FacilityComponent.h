// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Economy/SimEco_StepListener.h"
#include "GameplayTagContainer.h"
#include "SimEco_FacilityComponent.generated.h"

class USimEco_FacilityComponent;

/** Lifecycle state of a queued production job (the SHAPE that replicates). */
UENUM(BlueprintType)
enum class ESimEco_JobState : uint8
{
	/** Queued behind earlier jobs; inputs not yet reserved. */
	Pending,
	/** The head job: inputs reserved, progressing toward completion. */
	Running,
	/** Finished this session (kept transiently until pruned). */
	Completed,
	/** Cancelled; reserved inputs released. */
	Cancelled
};

/**
 * One replicated production-job entry. Only the SHAPE replicates — the recipe tag, the job's state,
 * and the fixed-step index at which the running job started. Clients derive smooth progress locally
 * from StartStepIndex and the recipe's step count (resolved from the data registry), so per-frame
 * progress never crosses the wire. Reserved input quantities are SERVER-ONLY and never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_JobEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Stable id so a specific job can be addressed (cancel by id) regardless of array reordering. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	int32 JobId = 0;

	/** Recipe/process identity (child of SimEco.Recipe / a process-def DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	FGameplayTag RecipeTag;

	/** Current lifecycle state. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	ESimEco_JobState State = ESimEco_JobState::Pending;

	/** Fixed-step index at which this job started running (-1 while pending). For client progress. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	int64 StartStepIndex = -1;

	/** Number of fixed steps the running job requires (replicated so clients can show progress). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	int32 StepsRequired = 0;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const struct FSimEco_JobArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_JobArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_JobArray& InArraySerializer);
};

/** Fast-array serializer holding the facility's queued jobs. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_JobArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated job entries (queue order = array order). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Facility")
	TArray<FSimEco_JobEntry> Entries;

	/** Non-replicated back-pointer for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimEco_FacilityComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_JobEntry, FSimEco_JobArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the job array. */
template<>
struct TStructOpsTypeTraits<FSimEco_JobArray> : public TStructOpsTypeTraitsBase2<FSimEco_JobArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) when the job queue's shape changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnQueueChanged, USimEco_FacilityComponent*, Facility);

/** Broadcast on the server when a job finishes producing its outputs. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimEco_OnJobCompleted, USimEco_FacilityComponent*, Facility, FGameplayTag, RecipeTag);

/**
 * A facility that runs a queue of production jobs, advanced by the economy fixed-step driver.
 *
 * Replicates only the QUEUE SHAPE (recipe tag, state, start-step, steps-required) plus the start
 * tick of the running job; clients derive smooth progress locally from the replicated start step.
 * Reserved input quantities are SERVER-ONLY (never replicated). The component implements
 * ISimEco_StepListener and registers with the economy driver, which calls AdvanceEconomyStep once
 * per fixed step to progress the head job; on completion it emits outputs and fires OnJobCompleted.
 *
 * All mutators (StartJob/CancelJob) are AUTHORITY ONLY and early-return on clients. CancelJob on the
 * running job calls ReleaseReserved so reserved inputs are returned.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_FacilityComponent
	: public UActorComponent
	, public ISimEco_StepListener
{
	GENERATED_BODY()

public:
	USimEco_FacilityComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISimEco_StepListener
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) override;
	//~ End ISimEco_StepListener

	/**
	 * Enqueue a production job for RecipeTag. AUTHORITY ONLY. Returns the new job's id, or
	 * INDEX_NONE if rejected (no authority / invalid recipe / queue full). The job becomes Running
	 * (reserving inputs) when it reaches the head of the queue during a fixed step.
	 *
	 * @param RecipeTag     Process identity to run.
	 * @param StepsRequired Number of fixed steps the job takes (a per-job tunable; clamped to >= 1).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Facility")
	int32 StartJob(FGameplayTag RecipeTag, int32 StepsRequired = 1);

	/**
	 * Cancel the job with JobId (running or pending). AUTHORITY ONLY. If it was running, its reserved
	 * inputs are released (ReleaseReserved). Returns true if a matching job was found and cancelled.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Facility")
	bool CancelJob(int32 JobId);

	/** Number of jobs currently in the queue (any state not yet pruned). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Facility")
	int32 GetQueueLength() const { return Jobs.Entries.Num(); }

	/** Snapshot of the queued jobs (read-only; safe on clients). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Facility")
	TArray<FSimEco_JobEntry> GetJobs() const { return Jobs.Entries; }

	/** Normalized [0,1] progress of the running job, derived from the replicated start step. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Facility")
	float GetRunningJobProgress() const;

	/** Per-job input requirements (commodity -> quantity) reserved when the job starts running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Facility")
	TMap<FGameplayTag, double> DefaultInputs;

	/** Per-job outputs (commodity -> quantity) emitted as market sell orders when a job completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Facility")
	TMap<FGameplayTag, double> DefaultOutputs;

	/** Maximum number of queued jobs. A tunable, not a magic number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Facility",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxQueueLength = 8;

	/** Fired (server and clients) when the queue shape changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Facility")
	FSimEco_OnQueueChanged OnQueueChanged;

	/** Fired on the server when a job completes and emits its outputs. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Facility")
	FSimEco_OnJobCompleted OnJobCompleted;

	/** Called by the fast-array entry callbacks on clients to surface a queue-shape change. */
	void HandleReplicatedChange();

private:
	/** Replicated job-queue SHAPE. */
	UPROPERTY(Replicated)
	FSimEco_JobArray Jobs;

	/**
	 * Inputs reserved for the currently-running head job (SERVER-ONLY; never replicated). Reserving
	 * means these quantities are committed to the job and released back on cancel.
	 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, double> ReservedInputs;

	/** Monotonic job-id source (server-only). */
	int32 NextJobId = 1;

	/** True once this facility has registered with the economy driver. */
	bool bRegisteredWithDriver = false;

	/** Promote the head pending job to Running, reserving its inputs. Server-only. */
	void TryStartHeadJob(int64 CurrentStepIndex);

	/** Complete the running head job: emit outputs as sell orders, fire OnJobCompleted, dequeue. */
	void CompleteHeadJob();

	/** Release any inputs reserved for the running job back (clears ReservedInputs). Server-only. */
	void ReleaseReserved();

	/** Emit DefaultOutputs as market sell orders for the owning actor. Server-only. */
	void EmitOutputsToMarket();

	/** Mark an entry dirty and broadcast a queue-shape change (server helper). */
	void MarkJobDirtyAndNotify(FSimEco_JobEntry& Entry);

	/** Register/unregister with the world's economy driver (authority only). */
	void RegisterWithDriver();
	void UnregisterFromDriver();
};
