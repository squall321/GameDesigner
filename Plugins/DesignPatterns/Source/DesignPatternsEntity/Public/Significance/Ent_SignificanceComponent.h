// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Significance/Ent_SignificanceSettings.h"
#include "Ent_SignificanceComponent.generated.h"

/**
 * Fired (locally) when the entity's significance bucket changes, so the owner can swap meshes/LODs/AI
 * fidelity. Carries the new bucket and detail level.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEnt_OnSignificanceChanged,
	EEnt_SignificanceBucket, NewBucket, int32, DetailLevel, float, TickInterval);

/**
 * Per-entity significance receiver.
 *
 * Registers with the world UEnt_SignificanceManagerSubsystem, which periodically scores every registered
 * entity and pushes a bucket/tick-interval/detail-level down through SetBucket. The component applies the
 * tick interval to its OWNER's primary tick and broadcasts OnSignificanceChanged.
 *
 * Significance is a LOCAL, cosmetic/performance concern — it is NOT replicated. Each machine computes its
 * own significance relative to its own viewer.
 */
UCLASS(ClassGroup = (DesignPatternsEntity), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Cooking, Collision, Replication))
class DESIGNPATTERNSENTITY_API UEnt_SignificanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnt_SignificanceComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Relative importance weight applied to this entity's score (a closer, heavier entity ranks higher
	 * under count budgets). Authored tunable; no hardcoded gameplay numbers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance", meta = (ClampMin = "0.0"))
	float ImportanceWeight = 1.f;

	/**
	 * Called by the manager to set this entity's current bucket. Applies TickInterval to the owner's
	 * primary actor tick (when bDriveOwnerTick) and broadcasts OnSignificanceChanged on a change.
	 */
	void SetBucket(EEnt_SignificanceBucket NewBucket, float TickInterval, int32 DetailLevel);

	/** The entity's current significance bucket. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Significance")
	EEnt_SignificanceBucket GetBucket() const { return CurrentBucket; }

	/** The current detail level pushed by the manager. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Significance")
	int32 GetDetailLevel() const { return CurrentDetailLevel; }

	/** When true, applying a bucket also sets the owner's PrimaryActorTick.TickInterval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance")
	bool bDriveOwnerTick = true;

	/** Fired (locally) when the bucket changes. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Significance")
	FEnt_OnSignificanceChanged OnSignificanceChanged;

private:
	/** Current bucket (defaults to High until the manager scores us). */
	EEnt_SignificanceBucket CurrentBucket = EEnt_SignificanceBucket::High;

	/** Current detail level. */
	int32 CurrentDetailLevel = 0;

	/** True once registered with the manager. */
	bool bRegistered = false;
};
