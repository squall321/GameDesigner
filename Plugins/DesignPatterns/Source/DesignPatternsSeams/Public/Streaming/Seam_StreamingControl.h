// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_StreamingControl.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_StreamingControl : public UInterface
{
	GENERATED_BODY()
};

/**
 * Command + aggregate-progress seam over the engine's level-streaming machinery.
 *
 * The LevelDirector module ships the concrete adapter (WORLD-lifetime, registered WeakObserved into the
 * GameInstance service locator under FlowTags::Service_StreamingControl). That adapter WRAPS THE ENGINE:
 * it drives ULevelStreaming::SetShouldBeLoaded/SetShouldBeVisible and/or registers a synthetic interest
 * source through the existing streaming-director subsystem, and computes progress by polling
 * ULevelStreaming load/visibility state. It adds NO new method to the streaming-director subsystem and
 * never reinvents streaming.
 *
 * The GameFlow loading-screen coordinator consumes this to request the active phase's content resident
 * and to feed a true aggregate progress bar — it RE-RESOLVES the adapter per load (the adapter is
 * world-lifetime, so a cached pointer would dangle across travel) and never depends on the LevelDirector
 * module's concrete type. Degrades inert (preload-only loading screen) when no adapter is registered.
 *
 * Categories are FGameplayTags (a designer-authored content grouping the adapter maps to concrete
 * sublevels), keeping the seam free of any engine streaming type and DesignPatternsSeams a leaf module.
 */
class DESIGNPATTERNSSEAMS_API ISeam_StreamingControl
{
	GENERATED_BODY()

public:
	/**
	 * Request that every sublevel tagged with any of Categories becomes loaded + visible. Idempotent: a
	 * second request supersedes the previous one (the adapter tracks a single active request set). Empty
	 * Categories releases the current request (equivalent to ReleaseRequest).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Streaming")
	void RequestLevelsResident(const FGameplayTagContainer& Categories);

	/**
	 * Aggregate normalized progress in [0,1] across all currently-requested sublevels (1.0 when nothing is
	 * requested, so a caller treats "no streaming work" as instantly complete). Defensive default 1.0.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Streaming")
	float GetAggregateProgress() const;

	/** True once every requested sublevel is loaded AND visible. Defensive default true (nothing pending). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Streaming")
	bool AreRequestedLevelsReady() const;

	/** Drop the current residency request (the adapter may then allow the engine to unload those sublevels). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Streaming")
	void ReleaseRequest();
};
