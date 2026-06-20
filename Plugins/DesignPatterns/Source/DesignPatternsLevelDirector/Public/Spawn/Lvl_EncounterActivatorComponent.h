// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"
#include "Lvl_EncounterActivatorComponent.generated.h"

class UDP_ServiceLocatorSubsystem;
class APawn;

/** Broadcast on the owner when this region's encounter (de)activates, for local listeners/Blueprints. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLvl_OnEncounterActiveChanged, bool, bActive);

/**
 * Activates / deactivates a REGION'S ENCOUNTER based on two conditions, BOTH of which must hold:
 *   1. The activation gate is open for GateKey (ISeam_ActivationGate; DEFAULT OPEN when unresolved).
 *   2. An "interested" actor (by default any local player pawn) is within ActivationRadius — with a
 *      DeactivationRadius hysteresis band so the encounter does not flicker at the boundary.
 *
 * This is a CHEAP polling component: it does not spawn anything itself. It flips a boolean active
 * state, fires OnEncounterActiveChanged (and a message-bus event), and exposes IsEncounterActive()
 * so a placer / spawn director / Blueprint can react (e.g. run GeneratePlacement on activation, or
 * stream a sublevel in). It is LOCAL/per-machine by design: proximity is evaluated on each machine
 * against its own viewers, so cosmetic activation is responsive on clients; authoritative spawning
 * still gates on authority in whatever system listens to the event.
 *
 * REMOVABILITY: with the gate seam unresolved the gate is treated as open, so the encounter activates
 * purely on proximity — the component works standalone without the World hub.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_EncounterActivatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULvl_EncounterActivatorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	// ---- Configuration --------------------------------------------------------------------------

	/** Region this activator represents; carried in the broadcast event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter")
	FGameplayTag RegionTag;

	/**
	 * Activation gate key evaluated via ISeam_ActivationGate. Unset -> ungated (gate always open).
	 * Default-open when the gate seam is unresolved.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter")
	FGameplayTag GateKey;

	/** Distance (cm) within which an interested actor activates the encounter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter",
		meta = (ClampMin = "0.0"))
	float ActivationRadius = 4000.0f;

	/**
	 * Distance (cm) beyond which the encounter deactivates. Must be >= ActivationRadius to form a
	 * hysteresis band (clamped up at use). Prevents flicker when a viewer hovers near the edge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter",
		meta = (ClampMin = "0.0"))
	float DeactivationRadius = 5000.0f;

	/** Seconds between proximity polls. The component does not poll every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter",
		meta = (ClampMin = "0.02"))
	float PollInterval = 0.5f;

	/**
	 * If true, deactivating the encounter (gate closed or all viewers left) is allowed. If false, the
	 * encounter latches ON the first time it activates and never deactivates (one-shot reveal).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Encounter")
	bool bAllowDeactivation = true;

	/** Fires locally whenever the active state flips. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Lvl|Encounter")
	FLvl_OnEncounterActiveChanged OnEncounterActiveChanged;

	// ---- Queries --------------------------------------------------------------------------------

	/** True while the encounter is currently active (gate open AND a viewer is in range). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Encounter")
	bool IsEncounterActive() const { return bActive; }

	/** Force a re-evaluation immediately (e.g. after a gate flips). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Encounter")
	void Reevaluate();

private:
	/** Current active state. Local/per-machine; not replicated. */
	UPROPERTY(Transient)
	bool bActive = false;

	/** Accumulates real time toward the next poll. */
	float PollAccumulator = 0.f;

	/** Resolve the GameInstance service locator (null-safe). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Ask the activation gate seam; DEFAULT OPEN when unresolved or GateKey invalid. */
	bool IsGateOpen() const;

	/**
	 * Distance (cm) from this component's owner to the NEAREST interested actor, or a huge value if
	 * none. By default interested actors are the local player pawns.
	 */
	float DistanceToNearestInterest() const;

	/** Apply the gate + proximity rule and flip state with hysteresis; fires events on change. */
	void EvaluateActivation();

	/** Set the active state and broadcast the change (delegate + message bus). */
	void SetActive(bool bNewActive);

	/** Broadcast the (de)activation on the message bus. */
	void BroadcastEncounterEvent(bool bNowActive) const;
};
