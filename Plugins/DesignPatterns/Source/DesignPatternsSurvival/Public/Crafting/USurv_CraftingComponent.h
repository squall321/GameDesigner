// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "USurv_CraftingComponent.generated.h"

class USurv_Recipe;
class USurv_ResourceStoreComponent;

/** One entry in the craft queue: the recipe tag plus the world time it will finish. */
USTRUCT(BlueprintType)
struct FSurv_CraftJob
{
	GENERATED_BODY()

	/** DataTag identifying the queued recipe (resolved through the core data registry). */
	UPROPERTY(BlueprintReadOnly, Category = "Survival|Crafting")
	FGameplayTag RecipeTag;

	/** World seconds at which this job completes (only meaningful once it reaches the front). */
	UPROPERTY(BlueprintReadOnly, Category = "Survival|Crafting")
	float CompleteWorldTime = 0.f;

	/** True once inputs have been consumed and the timer is running for this (front) job. */
	UPROPERTY(BlueprintReadOnly, Category = "Survival|Crafting")
	bool bStarted = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnCraftQueued, FGameplayTag, RecipeTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnCraftCompleted, FGameplayTag, RecipeTag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnCraftCancelled, FGameplayTag, RecipeTag, bool, bRefunded);

/**
 * Queue-based crafting component.
 *
 * Resolves recipes by tag through the core UDP_DataRegistrySubsystem (no asset-path coupling),
 * validates the required station and available inputs, then runs jobs one at a time on the world
 * timer. AUTHORITY-ONLY for StartCraft/CancelCraft and all queue mutation. Inputs are consumed
 * from / outputs deposited into a USurv_ResourceStoreComponent (the module's lightweight store;
 * swap for RPG inventory at the marked seam).
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_CraftingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USurv_CraftingComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Queue a craft of the recipe identified by RecipeTag. AUTHORITY-ONLY. Returns true if queued.
	 * Validation (station + inputs) for the head job happens when it actually starts; queuing a job
	 * that later can't pay its inputs simply fails to start and is dropped with a warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	bool StartCraft(FGameplayTag RecipeTag);

	/**
	 * Cancel the queued job at QueueIndex. AUTHORITY-ONLY. If the cancelled job had already started
	 * (inputs consumed), its inputs are refunded to the store. Returns true if a job was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	bool CancelCraft(int32 QueueIndex);

	/** Cancel every queued job, refunding any in-progress inputs. AUTHORITY-ONLY. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	void CancelAll();

	/** Snapshot of the current queue (front = currently crafting). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Crafting")
	const TArray<FSurv_CraftJob>& GetQueue() const { return Queue; }

	/** Normalized progress 0..1 of the front job (0 if idle). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Crafting")
	float GetActiveProgress() const;

	/**
	 * The set of station tags currently available to this crafter (e.g. set when overlapping a
	 * workbench). A recipe's RequiredStationTag must be matched (hierarchy-aware) by one of these,
	 * unless the recipe requires no station. Server-authoritative.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	void SetAvailableStations(const FGameplayTagContainer& Stations);

	/** The resource store crafting draws inputs from and deposits outputs into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting")
	TObjectPtr<USurv_ResourceStoreComponent> ResourceStore;

	/** Fired when a job is accepted into the queue. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Crafting")
	FSurv_OnCraftQueued OnCraftQueued;

	/** Fired when a job finishes and its outputs are deposited. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Crafting")
	FSurv_OnCraftCompleted OnCraftCompleted;

	/** Fired when a job is cancelled. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Crafting")
	FSurv_OnCraftCancelled OnCraftCancelled;

protected:
	/** Pending + active craft jobs. Index 0 is the active job. */
	UPROPERTY()
	TArray<FSurv_CraftJob> Queue;

	/** Station tags currently available to this crafter. */
	UPROPERTY()
	FGameplayTagContainer AvailableStations;

	/** Timer driving completion of the front job. */
	FTimerHandle CraftTimerHandle;

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Resolve a recipe tag to its asset via the core data registry (synchronous load). */
	USurv_Recipe* ResolveRecipe(const FGameplayTag& RecipeTag) const;

	/** True if AvailableStations satisfies Recipe's RequiredStationTag (empty requirement = always ok). */
	bool HasRequiredStation(const USurv_Recipe* Recipe) const;

	/** True if the store holds every input stack of Recipe. */
	bool CanAfford(const USurv_Recipe* Recipe) const;

	/** Start the front job if one exists and is not yet started: validate, consume inputs, set timer. */
	void TryStartFrontJob();

	/** Timer callback: complete the front job (deposit outputs), pop it, then start the next. */
	void HandleCraftComplete();

	/** Current world time seconds (0 if no world). */
	float GetWorldTimeSeconds() const;

	/** Refund a started job's inputs back into the store (server only). */
	void RefundInputs(const FGameplayTag& RecipeTag);
};
