// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "USurv_DurabilityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnDurabilityChanged, float, NewDurability);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSurv_OnBroken);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSurv_OnRepaired);

/**
 * Tool/equipment wear tracker.
 *
 * Replicated current durability so clients can show a wear bar / broken state. All wear and repair
 * is AUTHORITY-ONLY. When durability hits zero the item is "broken" (OnBroken); repairing above
 * zero fires OnRepaired. The owning gameplay code decides what "broken" means (disable swing,
 * unequip, etc.) by binding the delegates.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_DurabilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USurv_DurabilityComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Apply Amount of wear (Amount > 0 reduces durability). AUTHORITY-ONLY. Returns the new value. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Durability")
	float ApplyWear(float Amount);

	/** Restore Amount of durability, clamped to MaxDurability. AUTHORITY-ONLY. Returns the new value. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Durability")
	float Repair(float Amount);

	/** Current durability. Safe on clients (replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Durability")
	float GetDurability() const { return CurrentDurability; }

	/** Normalized 0..1 durability. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Durability")
	float GetDurabilityNormalized() const;

	/** True if durability has reached zero. Safe on clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Durability")
	bool IsBroken() const { return CurrentDurability <= 0.f; }

	/** Full durability when fresh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Durability", meta = (ClampMin = "1.0"))
	float MaxDurability = 100.f;

	/** Fired (server + clients via OnRep) when durability changes. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Durability")
	FSurv_OnDurabilityChanged OnDurabilityChanged;

	/** Fired when durability reaches zero. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Durability")
	FSurv_OnBroken OnBroken;

	/** Fired when a broken item is repaired above zero. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Durability")
	FSurv_OnRepaired OnRepaired;

protected:
	/** Replicated current durability. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentDurability)
	float CurrentDurability = 100.f;

	/** Tracks the last broken state to edge-trigger OnBroken/OnRepaired across replication. */
	bool bWasBroken = false;

	UFUNCTION()
	void OnRep_CurrentDurability();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Fire OnDurabilityChanged and any broken/repaired edge events. */
	void NotifyChanged();
};
