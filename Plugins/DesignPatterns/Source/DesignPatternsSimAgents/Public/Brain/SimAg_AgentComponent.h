// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Persist/Seam_Persistable.h"
#include "Brain/SimAg_Agent.h"
#include "Jobs/SimAg_JobTypes.h"
#include "SimAg_AgentComponent.generated.h"

class UDP_StrategySelector;
class UDP_Blackboard;
class IDP_BlackboardProvider;

/**
 * Fired (server and clients) when this agent's replicated CurrentActivity changes.
 * @param Agent       The agent component.
 * @param NewActivity The activity now in effect.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnActivityChanged,
	USimAg_AgentComponent*, Agent, FGameplayTag, NewActivity);

/**
 * The agent "brain" component: owns a utility-AI strategy selector and a blackboard, runs the decision
 * loop on authority at the settings-driven cadence, and exposes the agent through the ISimAg_Agent seam.
 * Also a save participant (ISeam_Persistable).
 *
 * OWNERSHIP / GC: it OWNS its Brain (UPROPERTY Instanced TObjectPtr) and its Blackboard (a constructed
 * subobject, UPROPERTY TObjectPtr), so the FDP_StrategyContext handed to strategies is always valid.
 * The blackboard is created in the constructor and never null at runtime.
 *
 * REPLICATION: SetIsReplicatedByDefault(true). The only replicated state is CurrentActivity
 * (ReplicatedUsing), the cheap, designer-readable summary of what the agent is doing — derived on the
 * server by the brain. Every mutator of authoritative state guards authority at the top and early-returns
 * on clients. The brain runs ONLY on authority; clients just observe the replicated activity.
 *
 * The component never hard-depends on sibling capability components: it resolves the need/scheduler/job
 * provider seams off its owning actor each decision pass (so a Survival needs adapter, a schedule
 * component, and the job board all plug in through interfaces).
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_AgentComponent
	: public UActorComponent
	, public ISimAg_Agent
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	USimAg_AgentComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISimAg_Agent
	virtual FGameplayTag GetAgentTag_Implementation() const override;
	virtual FGameplayTag GetCurrentActivity_Implementation() const override;
	virtual FVector GetHomeLocation_Implementation() const override;
	virtual FVector GetWorkLocation_Implementation() const override;
	//~ End ISimAg_Agent

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	/**
	 * Set the agent's current activity. AUTHORITY ONLY: early-returns on clients. Called by the brain's
	 * strategies on Execute; replicates to clients and fires OnActivityChanged on both sides.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Agent")
	void SetCurrentActivity(FGameplayTag NewActivity);

	/**
	 * Record that the agent has claimed a job (mirrors the handle into a brain-readable flag and stores
	 * the id for completion). AUTHORITY ONLY. Pass an invalid handle to clear.
	 */
	void SetClaimedJob(const FSimAg_JobHandle& Handle);

	/** The job id this agent currently holds, or an invalid guid if none. */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Agent")
	FGuid GetClaimedJobId() const { return ClaimedJobId; }

	/** The owned blackboard (never null at runtime). Strategies and steering read/write through it. */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Agent")
	UDP_Blackboard* GetBlackboard() const { return Blackboard; }

	/** The blackboard as the provider seam, for building a FDP_StrategyContext. */
	TScriptInterface<IDP_BlackboardProvider> GetBlackboardProvider() const;

	/** Stable id of this agent (assigned on authority at BeginPlay; persisted). */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Agent")
	FSeam_EntityId GetAgentId() const { return AgentId; }

	/** Fired when CurrentActivity changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Agent")
	FSimAg_OnActivityChanged OnActivityChanged;

	/** The agent's archetype/role tag (child of a project's agent root). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Agent")
	FGameplayTag AgentTag;

	/**
	 * The utility-AI brain. EditInline-owned (Instanced): authored per-agent in the editor, or left null
	 * to fall back to the settings DefaultBrainClass at BeginPlay. The FDP_StrategyContext's selector.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "SimAgents|Agent")
	TObjectPtr<UDP_StrategySelector> Brain;

	/** World-space home anchor (where the agent sleeps/idles). Drives Home-sourced strategy targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Agent")
	FVector HomeLocation = FVector::ZeroVector;

	/** World-space work anchor (where schedule/jobs send the agent). Drives Work-sourced targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Agent")
	FVector WorkLocation = FVector::ZeroVector;

private:
	/** The owned blackboard subobject. Constructed in the ctor; UPROPERTY keeps it GC-alive. Never null. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimAgents|Agent", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDP_Blackboard> Blackboard;

	/**
	 * The activity the agent is currently performing. The single replicated piece of agent state, derived
	 * by the brain on the server. ReplicatedUsing so clients fire OnActivityChanged on update.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentActivity)
	FGameplayTag CurrentActivity;

	/** Stable id (authority-assigned, persisted). Not replicated; identity is reconstructed per machine. */
	UPROPERTY(Transient)
	FSeam_EntityId AgentId;

	/** The job id currently claimed by this agent (authority bookkeeping; not replicated). */
	UPROPERTY(Transient)
	FGuid ClaimedJobId;

	/** Seconds since the last decision evaluation, for the settings-driven cadence. */
	float DecisionAccumulator = 0.f;

	/** Cached decision period (1 / DecisionTickHz) from settings. */
	float DecisionPeriod = 0.5f;

	/** OnRep for CurrentActivity: fires OnActivityChanged on clients. */
	UFUNCTION()
	void OnRep_CurrentActivity();

	/** True on the server / standalone (mirrors the world-authority rule for an owned actor). */
	bool HasOwnerAuthority() const;

	/** Build the strategy context (owner + blackboard) and run the selector's Select+Execute. */
	void RunDecision();

	/** Ensure Brain is non-null, instancing the settings DefaultBrainClass if it was left unset. */
	void EnsureBrain();
};
