// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Economy/Seam_ResourceProducer.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "UBuild_FacilityProducerComponent.generated.h"

/**
 * One cyclic production process a placed building can run: the process tag, how long a cycle takes,
 * and what it yields per completed cycle. Purely data; configured per facility.
 */
USTRUCT(BlueprintType)
struct FBuild_ProductionProcess
{
	GENERATED_BODY()

	/** Identity tag of this process (child of e.g. Surv.Build process namespace). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	FGameplayTag ProcessTag;

	/** Seconds per cycle (defensive default; designer-tunable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build", meta = (ClampMin = "0.1"))
	float CycleSeconds = 10.f;

	/** Resource stacks deposited into the output store per completed cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	TArray<FSurv_ResourceStack> Outputs;

	FBuild_ProductionProcess() = default;
};

/**
 * Presents a placed building as a resource producer through the PROMOTED seam ISeam_ResourceProducer,
 * so markets / aggregators / the economy can read it WITHOUT this module depending on SimEconomy.
 *
 * Authority-only control: SetActiveProcess starts a repeating world-timer cycle; on each completion it
 * deposits the active process's outputs into the co-located USurv_ResourceStoreComponent and starts the
 * next cycle. Only the (process tag, server cycle-start time) replicate; clients derive progress from
 * the replicated start time and never mutate state.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API UBuild_FacilityProducerComponent
	: public UActorComponent
	, public ISeam_ResourceProducer
{
	GENERATED_BODY()

public:
	UBuild_FacilityProducerComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- ISeam_ResourceProducer ----
	virtual FGameplayTag GetActiveProcessTag_Implementation() const override { return ActiveProcessTag; }
	virtual bool IsProducing_Implementation() const override { return ActiveProcessTag.IsValid(); }
	virtual float GetProductionProgress_Implementation() const override;
	virtual void GetExpectedOutputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const override;
	virtual bool SetActiveProcess_Implementation(FGameplayTag ProcessTag) override;
	virtual void CancelProduction_Implementation() override;

	/** The processes this facility can run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	TArray<FBuild_ProductionProcess> Processes;

	/** Store completed-cycle outputs are deposited into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	TObjectPtr<USurv_ResourceStoreComponent> OutputStore;

	/**
	 * If true the facility auto-starts the first configured process on BeginPlay (authority-only).
	 * Otherwise it stays idle until SetActiveProcess is called.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	bool bAutoStartFirstProcess = false;

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** Active process tag (replicated so clients can show what is running). */
	UPROPERTY(ReplicatedUsing = OnRep_Cycle)
	FGameplayTag ActiveProcessTag;

	/** Server world-time when the current cycle started (replicated for client-derived progress). */
	UPROPERTY(ReplicatedUsing = OnRep_Cycle)
	double CycleStartServerTime = 0.0;

	/** Cached length of the active cycle (server-side; clients re-derive from the process def). */
	float ActiveCycleSeconds = 0.f;

	/** Timer driving cycle completion. */
	FTimerHandle CycleTimerHandle;

	UFUNCTION()
	void OnRep_Cycle();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Current world time seconds (0 if no world). */
	double GetWorldTimeSeconds() const;

	/** Find the configured process by tag (nullable). */
	const FBuild_ProductionProcess* FindProcess(const FGameplayTag& ProcessTag) const;

	/** Server timer callback: deposit outputs, then start the next cycle of the same process. */
	void HandleCycleComplete();

	/** Start (or restart) a cycle for the active process. AUTHORITY-only. */
	void BeginCycle();
};
