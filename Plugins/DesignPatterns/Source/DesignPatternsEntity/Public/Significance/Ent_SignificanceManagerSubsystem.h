// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Engine/TimerHandle.h"
#include "Ent_SignificanceManagerSubsystem.generated.h"

class AActor;
class UEnt_SignificanceComponent;
class UEnt_SignificanceSettings;

/**
 * World LOD/significance budgeter.
 *
 * TIMER-DRIVEN (FTimerManager), deliberately NOT an FTickableGameObject — mirroring
 * UDP_ObjectPoolSubsystem so nothing ticks in editor/preview worlds and the recompute cadence is a
 * tunable period rather than per-frame.
 *
 * Each period it scores every registered significance component by distance from the significance source
 * (scaled by the component's ImportanceWeight), assigns each entity the finest band it fits (respecting
 * each band's count budget, overflow spilling to coarser bands), and pushes that band's tick-interval +
 * detail-level into the component. All thresholds come from the authored UEnt_SignificanceSettings.
 *
 * The significance source (the viewer to measure distance from) is held WEAKLY; if none is set the
 * subsystem falls back to the local player's pawn/camera each recompute.
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_SignificanceManagerSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Register a significance component so it participates in budgeting. Idempotent. */
	void RegisterSignificanceComponent(UEnt_SignificanceComponent* Component);

	/** Unregister a significance component. */
	void UnregisterSignificanceComponent(UEnt_SignificanceComponent* Component);

	/** Set the viewer actor distances are measured from (held weakly). Null = fall back to local player. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Significance")
	void SetSignificanceSource(AActor* Source);

	/** Force an immediate recompute (e.g. after a teleport). */
	UFUNCTION(BlueprintCallable, Category = "Entity|Significance")
	void RecomputeNow();

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Authored significance bands. Assign in defaults or at runtime; empty = everything stays High. */
	UPROPERTY(EditAnywhere, Category = "Entity|Significance")
	TObjectPtr<UEnt_SignificanceSettings> Settings;

	/** Seconds between recomputes. Tunable; a small value trades CPU for responsiveness. */
	UPROPERTY(EditAnywhere, Category = "Entity|Significance", meta = (ClampMin = "0.05"))
	float UpdatePeriodSeconds = 0.5f;

private:
	/** Registered components. Weak so the manager never keeps an entity alive. */
	TArray<TWeakObjectPtr<UEnt_SignificanceComponent>> Components;

	/** The viewer to measure from (weak). */
	TWeakObjectPtr<AActor> SignificanceSource;

	/** Recurring recompute timer (owned by this subsystem; cleared in Deinitialize). */
	FTimerHandle RecomputeTimerHandle;

	/** Timer callback wrapper around RecomputeNow. */
	void TimerRecompute();

	/** Resolve the world location to measure significance from (source, else local player). */
	bool GetSourceLocation(FVector& OutLocation) const;

	/** Start the recurring timer (if not already running and the world supports timers). */
	void EnsureTimer();
};
