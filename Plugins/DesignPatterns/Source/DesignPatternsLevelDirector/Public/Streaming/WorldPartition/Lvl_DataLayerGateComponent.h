// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Lvl_DataLayerGateComponent.generated.h"

/**
 * Maps a logical GateKey (queried through ISeam_ActivationGate, DEFAULT OPEN when unresolved) to
 * World Partition DATA-LAYER runtime activation.
 *
 * The HEADER deliberately names NO World-Partition / data-layer engine type: it carries only intent —
 * the FNames of the data-layer assets to activate, plus the gate key. UDataLayerManager and the data
 * layer instances are resolved and driven ENTIRELY in the .cpp (Engine module, already a dependency),
 * so this module never hard-depends on the WorldPartition module and degrades to a no-op when WP / data
 * layers are absent (exactly like the streaming director's soft WP resolve).
 *
 * AUTHORITY: data-layer RUNTIME state is server-authoritative — ApplyDataLayerState mutates it only with
 * authority, and clients receive the state through World Partition's own replication. Reevaluate may be
 * called on any machine but only the authority changes runtime state; clients just re-read the gate.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_DataLayerGateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULvl_DataLayerGateComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** True on server / standalone / listen-server host. Runtime data-layer mutation gates on this. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|DataLayer")
	bool HasWorldAuthority() const;

	// ---- Configuration --------------------------------------------------------------------------

	/**
	 * Gate key evaluated via ISeam_ActivationGate (default open when the gate seam is unresolved). When
	 * open, the listed data layers are activated; when closed, they are unloaded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|DataLayer")
	FGameplayTag GateKey;

	/**
	 * Asset names (the data-layer asset's FName, e.g. "DL_Interior") of the data layers this gate drives.
	 * Resolved against the world's UDataLayerManager in the .cpp — pure intent here, no engine type.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|DataLayer")
	TArray<FName> DataLayerAssetNames;

	/**
	 * If true, an open gate makes the layers ACTIVATED (loaded + visible); if false, an open gate only
	 * LOADS them (loaded-but-hidden). A closed gate always unloads. Lets a designer pre-warm vs reveal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|DataLayer")
	bool bActivateWhenOpen = true;

	/** If true, the component evaluates the gate once on BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|DataLayer")
	bool bReevaluateOnBeginPlay = true;

	// ---- Control --------------------------------------------------------------------------------

	/** Re-read the gate and apply the corresponding data-layer state (authority mutates; clients re-read). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|DataLayer")
	void Reevaluate();

	/** True if GateKey is currently open (ISeam_ActivationGate; default open when unresolved). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|DataLayer")
	bool IsGateOpen() const;

private:
	/**
	 * Drive the world's UDataLayerManager toward the desired state for the configured layers. Wrapped
	 * entirely here (resolved softly), so the header stays free of WP types. Authority-guarded by the
	 * caller; a no-op when WP / data layers are unavailable.
	 */
	void ApplyDataLayerState(bool bActive);

	/** Resolve the GameInstance service locator (null-safe). */
	class UDP_ServiceLocatorSubsystem* GetLocator() const;
};
