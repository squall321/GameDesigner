// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Tickable.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "History/Seam_HubHistory.h"
#include "Clock/Seam_SimClock.h"
#include "History/WorldHub_HistoryTypes.h"
#include "WorldHub_HistorySubsystem.generated.h"

class UWorldHub_StateHubSubsystem;

/**
 * Broadcast (server only — capture is authority-only) whenever a history frame is recorded.
 * @param FrameIndex The monotonic index assigned to the new frame.
 * @param LabelTag   The checkpoint label, or an invalid tag for an ordinary cadence/dirty frame.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWorldHub_OnFrameCaptured, int32, FrameIndex, FGameplayTag, LabelTag);

/**
 * TIME-TRAVEL / state history for the world hub.
 *
 * A non-replicated world subsystem that records a ring buffer of FWorldHub_Snapshot frames (reusing
 * the EXISTING ISeam_Persistable capture path on the hub) for rewind / replay / checkpoints.
 *
 * Timing is DETERMINISTIC and PAUSE-AWARE: it ticks via FTickableGameObject, advancing an accumulator
 * by DeltaTime * ISeam_SimClock::GetTimeScale() and skipping entirely while IsPaused(). A frame is
 * captured when the cadence elapses, or — when bCaptureOnChange is set — when the hub reported a value
 * change since the last frame.
 *
 * AUTHORITY MODEL: capture and restore are AUTHORITY ONLY (HasWorldAuthority gate at the TOP of each,
 * and IsTickable returns false on clients so no capture work runs there). Restore re-applies stored
 * snapshot entries through the hub's authoritative SetValue, so clients re-mirror the rewound state via
 * the normal replication path. Diffing is pure-read and safe on clients.
 *
 * Implements ISeam_HubHistory so replay/quest/debug tooling triggers rewind and reads the frame
 * timeline without including this header; it self-registers under DP.Service.WorldHub.History.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_HistorySubsystem
	: public UDP_WorldSubsystem
	, public FTickableGameObject
	, public ISeam_HubHistory
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own (matches the state hub). True on
	 * server / standalone / listen-host. Capture, restore and tick all gate on this.
	 */
	bool HasWorldAuthority() const;

	// ---- Capture / rewind (AUTHORITY ONLY) ----------------------------------------------------

	/**
	 * Capture the hub's current state as a new frame, optionally tagging it as a named checkpoint.
	 * AUTHORITY ONLY (no-op returning INDEX_NONE on clients / when the hub is gone).
	 * @return the new frame's monotonic FrameIndex, or INDEX_NONE on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	int32 CaptureFrame(FGameplayTag LabelTag);

	/** Rewind world state to the frame with the given monotonic index. AUTHORITY ONLY. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	bool RewindToFrame(int32 FrameIndex);

	/** Rewind to the most recent frame carrying CheckpointLabel. AUTHORITY ONLY. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	bool RewindToLabel(FGameplayTag CheckpointLabel);

	/**
	 * Rewind to the latest frame whose SimTimeSeconds is <= TargetSimTime (replay-to-timestamp).
	 * AUTHORITY ONLY. @return true if a frame at/before the time existed and was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	bool RewindToTimestamp(double TargetSimTime);

	// ---- Read / diff (safe on clients) --------------------------------------------------------

	/** Number of frames currently held in the ring buffer. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|History")
	int32 GetFrameCount() const { return Frames.Num(); }

	/** Copy the frame at the given monotonic index into Out. @return true if found. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	bool GetFrame(int32 FrameIndex, FWorldHub_HistoryFrame& Out) const;

	/** Compute the per-slot diff between two frames (by monotonic index). Out is reset first. */
	void DiffFrames(int32 FromIndex, int32 ToIndex, TArray<FWorldHub_StateDelta>& Out) const;

	/** BP-facing diff wrapper. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|History")
	void GetFrameDiff(int32 FromIndex, int32 ToIndex, TArray<FWorldHub_StateDelta>& OutDeltas) const { DiffFrames(FromIndex, ToIndex, OutDeltas); }

	// ---- ISeam_HubHistory ---------------------------------------------------------------------
	virtual bool RewindToCheckpoint(FGameplayTag CheckpointLabel) override;
	virtual int64 GetLatestEventSequence() const override;
	virtual void GetEventsSince(int64 Sequence, TArray<FInstancedStruct>& OutFlattened) const override;

	// ---- FTickableGameObject ------------------------------------------------------------------
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Fired (server) after a frame is captured. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|History")
	FWorldHub_OnFrameCaptured OnFrameCaptured;

	// ---- Tunables (no magic numbers; data-driven defaults) ------------------------------------

	/** Maximum frames retained; oldest evicted past this. Defaults are conservative and editable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"), Category = "DesignPatterns|WorldHub|History")
	int32 MaxFrames = 64;

	/** Simulation seconds between automatic cadence captures. <= 0 disables the cadence (manual only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|History")
	float CaptureCadenceSeconds = 30.0f;

	/** When true an automatic frame is also captured whenever the hub reported a change since the last frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|History")
	bool bCaptureOnChange = true;

private:
	/** Ring buffer of frames (oldest at index 0). FInstancedStruct payloads inside are LOCAL/SAVE only. */
	UPROPERTY()
	TArray<FWorldHub_HistoryFrame> Frames;

	/** The hub this records (re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> Hub;

	/** The shared simulation clock (re-resolved lazily; optional — falls back to unscaled DeltaTime). */
	TWeakInterfacePtr<ISeam_SimClock> Clock;
	TWeakObjectPtr<UObject> CachedClockObject;

	/** Monotonic frame-index counter (never reused). */
	int32 NextFrameIndex = 0;

	/** Simulation-time accumulator toward the next cadence capture. */
	float CadenceAccumulator = 0.0f;

	/** Running simulation seconds (advanced by DeltaTime * time-scale while not paused). */
	double SimClockSeconds = 0.0;

	/** Set true by the hub's OnValueChanged delegate; consumed by bCaptureOnChange. */
	bool bDirtySinceLastFrame = false;

	/** Resolve / cache the world hub (engine-native world-subsystem lookup). */
	UWorldHub_StateHubSubsystem* ResolveHub();

	/** Resolve / cache the simulation clock seam from the service locator (optional). */
	TScriptInterface<ISeam_SimClock> ResolveClock();

	/** Bound to the hub's OnValueChanged to mark the buffer dirty for change-driven capture. */
	UFUNCTION()
	void OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue);

	/** Apply a snapshot's entries back into the hub through its authoritative SetValue path. */
	void ApplySnapshotToHub(const FWorldHub_Snapshot& Snapshot);

	/** Find a frame's array position by monotonic index, or INDEX_NONE. */
	int32 FindFramePos(int32 FrameIndex) const;

	/** Broadcast frame-captured / rewound on the bus with a flat payload (if bus available). */
	void BroadcastHistoryBus(const FGameplayTag& Channel, int32 FrameIndex, const FGameplayTag& Label, double SimTime) const;

	/** Self-(un)register under DP.Service.WorldHub.History. */
	void RegisterSelfAsService(bool bRegister);
};
