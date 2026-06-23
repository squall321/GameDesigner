// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Crowd/SimAg_FlowField.h"
#include "SimAg_DensityFieldSubsystem.generated.h"

/**
 * One reported agent sample for the density field. Plain value (transient query state, never replicated).
 */
struct FSimAg_DensitySample
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Radius = 50.f;
};

/**
 * RVO-style, DENSITY-AWARE crowd avoidance that IMPLEMENTS the shipped ISimAg_FlowField seam and, on
 * Initialize, REGISTERS itself under the existing SimAgNativeTags::Service_FlowField (with override). The
 * shipped USimAg_FlowFieldSubsystem then discovers this as its external provider and forwards to it — so
 * steering gets richer, density-aware separation with ZERO steering edits and NO new service tag.
 *
 * It bins reported agents into a uniform grid so neighbour queries are O(local) for a large crowd, and
 * computes a reciprocal-velocity-style separation that accounts for both proximity AND closing velocity
 * (so head-on approaches push apart sooner). SampleFlowDirection delegates to the engine nav direction
 * (it is not a precomputed field generator). Transient state only; never replicated.
 *
 * THERE IS NO Service_DensityField TAG (it would be dead): this registers under Service_FlowField so the
 * existing forwarding path picks it up. Last writer under that key wins (documented).
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_DensityFieldSubsystem : public UDP_WorldSubsystem, public ISimAg_FlowField
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISimAg_FlowField
	virtual FVector SampleFlowDirection_Implementation(const FVector& WorldLocation, const FVector& Goal) const override;
	virtual FVector SampleSeparation_Implementation(const FVector& WorldLocation, float QueryRadius) const override;
	//~ End ISimAg_FlowField

	/**
	 * Report an agent's kinematics for this frame so neighbours can avoid it. Call once per agent per
	 * steering tick (e.g. from the steering component). The sample replaces any prior one at the same bin
	 * cell for that frame; samples are cleared each FlushFrame. Cheap, transient.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void ReportAgent(const FVector& Pos, const FVector& Velocity, float Radius);

	/** Clear all reported samples (call once at the start of a steering frame). */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void FlushFrame();

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Reported samples this frame, binned by integer cell for O(local) neighbour queries. */
	TMap<FIntVector, TArray<FSimAg_DensitySample>> Bins;

	/** Number of samples reported this frame (for debug). */
	int32 SampleCount = 0;

	/** Cell size (world units) of the spatial bin grid. Cached from settings (separation radius). */
	float BinSize = 150.f;

	/** Cached default separation radius from settings. */
	float DefaultSeparationRadius = 150.f;

	/** Service-locator key we registered under, for clean unregister on teardown. */
	FGameplayTag RegisteredServiceTag;

	/** Bin cell for a world position. */
	FIntVector CellOf(const FVector& Pos) const;

	/**
	 * Self-sufficient fallback used when no agent reported its kinematics this frame: scan the world's
	 * steering components (their public GetAgentLocation) and accumulate proximity separation, so the field
	 * works out-of-the-box without any steering edits or an external tick feeding ReportAgent.
	 */
	FVector SampleSeparationFromWorld(const FVector& WorldLocation, float Radius) const;
};
