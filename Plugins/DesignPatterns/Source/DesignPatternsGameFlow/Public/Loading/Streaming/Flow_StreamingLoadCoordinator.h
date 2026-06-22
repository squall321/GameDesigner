// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Flow_StreamingLoadCoordinator.generated.h"

class UFlow_LoadingScreenSubsystem;
class UFlow_LoadingViewModel;
class UDP_MessageBusSubsystem;

/**
 * Aggregates streaming-sublevel load progress into the EXISTING loading screen, on behalf of
 * UFlow_LoadingScreenSubsystem (which owns this as a UPROPERTY(Transient) subobject).
 *
 * When the active phase declares streaming content, this coordinator:
 *  - RE-RESOLVES ISeam_StreamingControl from the GI locator EACH load (never caches across travel — the
 *    LevelDirector adapter is world-lifetime, so a cached pointer would dangle after a map change),
 *  - requests the phase's UFlow_FlowStateDefinition::StreamingCategories resident through the seam,
 *  - polls the seam's aggregate progress on a timer and combines it with the loading screen's preload
 *    fraction by the global PreloadVsStreamWeight into ONE true bar,
 *  - feeds the combined fraction + label into the EXISTING UFlow_LoadingViewModel and broadcasts the
 *    EXISTING FFlow_LoadingProgressPayload on DP.Bus.Flow.LoadingProgress.
 *
 * Inert (the loading screen stays preload-only) when no streaming adapter resolves or no categories are
 * requested. Local/per-machine; never replicated.
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_StreamingLoadCoordinator : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to the owning loading-screen subsystem. */
	void Initialize(UFlow_LoadingScreenSubsystem* InOwner);

	/** Stop the aggregate timer + release any streaming request (called from the owner's Deinitialize). */
	void Shutdown();

	/**
	 * Begin aggregating a sublevel load for Categories, surfacing Label. Re-resolves the streaming adapter,
	 * requests the categories resident and starts the aggregate poll. No-op (inert) if Categories is empty
	 * or no adapter resolves.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading|Streaming")
	void BeginSublevelLoad(const FGameplayTagContainer& Categories, FText Label);

	/** Release the current streaming request and stop aggregating (e.g. on cancel / phase change). */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading|Streaming")
	void EndSublevelLoad();

	/** True while a sublevel aggregate load is in progress. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading|Streaming")
	bool IsAggregating() const { return bAggregating; }

	/** The most recent combined (preload + streaming) fraction in [0,1]. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading|Streaming")
	float GetCombinedProgress() const { return LastCombined; }

private:
	/**
	 * RE-RESOLVE the ISeam_StreamingControl provider from the GI locator (NEVER cached across travel), or
	 * null if no adapter is registered.
	 */
	UObject* ResolveStreamingForThisLoad() const;

	/** Resolve the owning GameInstance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Timer callback: poll aggregate progress, combine with preload, feed the VM + bus. */
	void TickAggregate();

	/** Combine the preload fraction (from the owner) and the streaming fraction by PreloadVsStreamWeight. */
	float CombineProgress(float StreamingFraction) const;

	// --- State ---

	/** Owning loading-screen subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlow_LoadingScreenSubsystem> Owner;

	/** Categories currently requested resident. */
	FGameplayTagContainer ActiveCategories;

	/** Status label surfaced while aggregating. */
	FText ActiveLabel;

	/** True while a sublevel aggregate load is in progress. */
	bool bAggregating = false;

	/** Last combined fraction. */
	float LastCombined = 0.f;

	/** Timer handle driving the aggregate poll. */
	FTimerHandle AggregateTimer;
};
