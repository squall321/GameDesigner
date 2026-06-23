// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/Lvl_InterestSource.h"
#include "Lvl_PredictivePrefetchComponent.generated.h"

class ULvl_StreamingDirectorSubsystem;
class ULvl_MemoryBudgetWatcherSubsystem;

/**
 * PLAYER-OWNED interest source that PREDICTIVELY widens its streaming bubble along the owner's velocity.
 *
 * Implements the in-module ILvl_InterestSource seam (allowed because it lives in this module) and
 * registers with the world streaming director via its public RegisterInterestSource / UnregisterInterestSource.
 *
 *   - GetInterestLocation LEADS the owner: it returns OwnerLocation + Velocity * LeadSeconds, clamped to
 *     MaxLeadDistance, so content streams in ahead of a fast mover before it arrives.
 *   - GetInterestRadius returns ONLY THE EXTRA radius (Speed * SpeedRadiusScale, >= 0) on top of the
 *     policy's distance bands — per the ILvl_InterestSource contract — so it never double-counts the base.
 *
 * Under memory pressure it reads the world ULvl_MemoryBudgetWatcherSubsystem's per-machine pressure for
 * its InterestCategory and SHRINKS its extra radius at the SOURCE (1 - pressure), so the existing
 * streaming director naturally unloads the now-out-of-range far content. It never commands a streaming
 * seam; clamping at the source keeps the watcher non-circular (LevelDirector is the streaming producer).
 *
 * PER-MACHINE: streaming is a local decision — this component is non-replicated and non-saved.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_PredictivePrefetchComponent : public UActorComponent, public ILvl_InterestSource
{
	GENERATED_BODY()

public:
	ULvl_PredictivePrefetchComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ILvl_InterestSource
	/** Owner location led by Velocity*LeadSeconds, clamped to MaxLeadDistance. */
	virtual FVector GetInterestLocation_Implementation() const override;
	/** EXTRA radius only (Speed*SpeedRadiusScale, >=0), shrunk by memory pressure on InterestCategory. */
	virtual float GetInterestRadius_Implementation() const override;
	//~ End ILvl_InterestSource

	// ---- Configuration --------------------------------------------------------------------------

	/** Seconds of velocity to lead the owner by (0 -> no lead; interest sits on the owner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch",
		meta = (ClampMin = "0.0", ForceUnits = "s"))
	float LeadSeconds = 1.5f;

	/** Maximum distance (cm) the interest point may lead the owner, however fast it moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MaxLeadDistance = 6000.0f;

	/** Extra streaming radius per unit speed (cm of radius per cm/s of speed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch",
		meta = (ClampMin = "0.0"))
	float SpeedRadiusScale = 0.5f;

	/** Hard cap on the EXTRA radius (cm) regardless of speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MaxExtraRadius = 12000.0f;

	/**
	 * Interest category used to query memory pressure from the watcher. When the watcher reports
	 * pressure for this category the extra radius is scaled down by (1 - pressure). Invalid -> queries
	 * the watcher's global pressure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch")
	FGameplayTag InterestCategory;

	/** If true, the component registers with the streaming director on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Prefetch")
	bool bAutoRegister = true;

private:
	/** Resolve the world streaming director (null-safe). */
	ULvl_StreamingDirectorSubsystem* GetDirector() const;

	/** Resolve the world memory-budget watcher (null-safe). */
	ULvl_MemoryBudgetWatcherSubsystem* GetMemoryWatcher() const;

	/** Owner velocity (cm/s), from the movement component when present, else 0. */
	FVector GetOwnerVelocity() const;

	/** True once we have registered with the director (so EndPlay unregisters exactly once). */
	bool bRegistered = false;
};
