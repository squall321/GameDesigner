// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Containers/Ticker.h"
#include "Audio_OcclusionService.generated.h"

class UAudio_OcclusionComponent;

/**
 * OCCLUSION (1). Centralized world subsystem that traces from the audio listener(s) to every
 * registered occlusion source on a round-robin per-sweep budget and pushes each source a target
 * occlusion factor.
 *
 * It reads the active listener position(s) from the engine audio device
 * (GetWorld()->GetAudioDeviceRaw() -> FAudioDevice::GetListeners) so it is SPLITSCREEN-CORRECT: a
 * source is occluded only when blocked from the NEAREST local listener (the one most likely to hear
 * it). It traces against the project occlusion channel using the world's line-trace. Sources are held
 * WEAKLY (world lifetime) and pruned when they die.
 *
 * Driven by an FTSTicker registered in Initialize and removed in Deinitialize (NOT FTickable), matching
 * the music director convention and avoiding editor / seamless-travel ticking. Defines its own world
 * authority check is unnecessary — occlusion is purely cosmetic/local and never mutates replicated
 * state, so it runs on every net mode that has an audio device (it self-disables on dedicated server
 * via the absence of an audio device).
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_OcclusionService : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Register a source so it is traced and driven by the service. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void RegisterSource(UAudio_OcclusionComponent* Source);

	/** Unregister a source (it keeps whatever occlusion factor it last had until it eases out). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void UnregisterSource(UAudio_OcclusionComponent* Source);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

protected:
	/** Occlusion is meaningless without an audio device; skip dedicated-server creation. */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
	/** Registered sources, WEAK (world lifetime). Pruned of dead entries each sweep. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UAudio_OcclusionComponent>> Sources;

	/** FTSTicker handle for the periodic sweep. Removed in Deinitialize (rule 3). */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Round-robin cursor into Sources so each sweep tests a budgeted slice. */
	int32 SweepCursor = 0;

	/** Seconds accumulated since the last sweep (gated by settings SweepInterval). */
	float SweepAccumulator = 0.f;

	/** Periodic tick: gate on SweepInterval, then trace a budgeted batch of sources. */
	bool Tick(float DeltaTime);

	/** Trace one source against the nearest listener and push its target occlusion factor. */
	void EvaluateSource(UAudio_OcclusionComponent* Source, const TArray<FVector>& ListenerLocations);

	/** Collect current audio listener world locations (splitscreen-aware). Empty if no device. */
	void GatherListenerLocations(TArray<FVector>& OutLocations) const;

	/** Drop dead weak source entries; keep SweepCursor valid. */
	void PruneSources();
};
