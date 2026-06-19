// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "USurv_ResourceStoreComponent.generated.h"

/**
 * One stack in the lightweight resource store: an item tag and an integer count.
 *
 * This is the SELF-CONTAINED inventory primitive for the Survival module. If the project later
 * depends on DesignPatternsRPG, replace consume/produce calls in USurv_CraftingComponent with
 * the RPG inventory at the marked seam; this struct can be deleted then.
 */
USTRUCT(BlueprintType)
struct FSurv_ResourceStack
{
	GENERATED_BODY()

	/** Item identity (e.g. Surv.Resource.Wood). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Resource")
	FGameplayTag ItemTag;

	/** How many of this item are held. Always >= 0 in a well-formed store. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Resource")
	int32 Count = 0;

	FSurv_ResourceStack() = default;
	FSurv_ResourceStack(const FGameplayTag& InTag, int32 InCount) : ItemTag(InTag), Count(InCount) {}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSurv_OnResourceChanged, FGameplayTag, ItemTag, int32, NewCount);

/**
 * A minimal, replicated bag of (item-tag -> count) resource stacks.
 *
 * This is the module's lightweight resource store — chosen over a hard dependency on
 * DesignPatternsRPG because that module is not present in this plugin. All mutators are
 * AUTHORITY-ONLY and the stacks replicate so clients can drive inventory UI. The crafting
 * component consumes inputs and deposits outputs through this component.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_ResourceStoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USurv_ResourceStoreComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Current count held for ItemTag (0 if none). Safe to call on clients (reads replicated state). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Resource")
	int32 GetCount(FGameplayTag ItemTag) const;

	/** True if the store holds at least Amount of ItemTag. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Resource")
	bool HasAtLeast(FGameplayTag ItemTag, int32 Amount) const;

	/**
	 * Add Amount of ItemTag (Amount must be > 0). AUTHORITY-ONLY: no-op on clients.
	 * Returns the new total for ItemTag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Resource")
	int32 AddResource(FGameplayTag ItemTag, int32 Amount);

	/**
	 * Remove up to Amount of ItemTag. AUTHORITY-ONLY: no-op on clients. Returns true only if the
	 * full Amount was available and removed (atomic — partial removals do not occur).
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Resource")
	bool RemoveResource(FGameplayTag ItemTag, int32 Amount);

	/** Snapshot of all non-empty stacks. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Resource")
	const TArray<FSurv_ResourceStack>& GetStacks() const { return Stacks; }

	/** Broadcast (locally, on server via mutation and on clients via OnRep) when a stack count changes. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Resource")
	FSurv_OnResourceChanged OnResourceChanged;

protected:
	/** Replicated set of resource stacks. Index is implementation detail; look up by tag. */
	UPROPERTY(ReplicatedUsing = OnRep_Stacks)
	TArray<FSurv_ResourceStack> Stacks;

	/** Mirror of Stacks captured at the last broadcast, used to diff on OnRep for change events. */
	TArray<FSurv_ResourceStack> LastBroadcastStacks;

	UFUNCTION()
	void OnRep_Stacks();

	/** True on the network authority (server / standalone). Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Find the index of ItemTag's stack, or INDEX_NONE. */
	int32 IndexOf(const FGameplayTag& ItemTag) const;

	/** Compare Stacks against LastBroadcastStacks and fire OnResourceChanged for each delta. */
	void BroadcastDeltas();
};
