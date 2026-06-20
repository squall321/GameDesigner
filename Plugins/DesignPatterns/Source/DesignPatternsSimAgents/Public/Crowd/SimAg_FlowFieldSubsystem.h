// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Crowd/SimAg_FlowField.h"
#include "SimAg_FlowFieldSubsystem.generated.h"

class USimAg_SteeringComponent;

/**
 * World-scoped crowd guidance provider and the module's built-in ISimAg_FlowField fallback.
 *
 * PASSTHROUGH + FALLBACK: if a game registers a richer external flow-field provider under
 * SimAgNativeTags::Service_FlowField, this subsystem forwards to it. Otherwise it answers queries itself
 * using a nav-mesh direction (engine NavigationSystem) for SampleFlowDirection and a registered-agent
 * neighbour scan for SampleSeparation — a cheap, correct fallback that needs no precomputed field.
 *
 * Steering components register/unregister their owning actor's location here so separation can be
 * computed without each component re-scanning the world. Registration is a non-owning weak set, pruned
 * lazily. Subsystems are never replicated; this holds only transient query state.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_FlowFieldSubsystem : public UDP_WorldSubsystem, public ISimAg_FlowField
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISimAg_FlowField (the fallback implementation; forwards to an external provider if present)
	virtual FVector SampleFlowDirection_Implementation(const FVector& WorldLocation, const FVector& Goal) const override;
	virtual FVector SampleSeparation_Implementation(const FVector& WorldLocation, float QueryRadius) const override;
	//~ End ISimAg_FlowField

	/** Register a steering component so its agent participates in neighbour separation. Non-owning. */
	void RegisterAgent(USimAg_SteeringComponent* Steering);

	/** Remove a steering component from the separation set. */
	void UnregisterAgent(USimAg_SteeringComponent* Steering);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Registered steering components whose owners participate in separation. WEAK (non-owning): a
	 * destroyed agent's entry self-invalidates and is pruned before use.
	 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<USimAg_SteeringComponent>> RegisteredAgents;

	/** Default neighbour radius (cached from settings). */
	float DefaultSeparationRadius = 150.f;

	/**
	 * Resolve an EXTERNAL flow-field provider registered under Service_FlowField, if one exists and is
	 * not this subsystem itself. Returns an invalid interface when only the fallback is available.
	 */
	TScriptInterface<ISimAg_FlowField> ResolveExternalProvider() const;

	/** Drop GC'd weak agent entries. Const: mutates only the transient registration set. */
	void PruneAgents() const;
};
