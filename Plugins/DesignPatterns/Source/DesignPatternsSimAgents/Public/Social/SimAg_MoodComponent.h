// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Mood/Seam_MoodProvider.h"
#include "Social/SimAg_MoodArray.h"
#include "SimAg_MoodComponent.generated.h"

class USimAg_ClockSubsystem;

/**
 * Message-bus payload broadcast when a mood axis changes notably. Carried as an FInstancedStruct on
 * SimAgNativeTags::Bus_MoodChanged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MoodEvent
{
	GENERATED_BODY()

	/** The axis that changed. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Mood")
	FGameplayTag Axis;

	/** New intensity in [0,1]. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Mood")
	float Intensity = 0.5f;
};

/** Fired (server and clients) after any mood axis changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnMoodChanged, USimAg_MoodComponent*, MoodComponent);

/**
 * Replicated per-agent emotion model implementing the shared ISeam_MoodProvider seam. Axes decay toward
 * their baseline on the simulation clock; events (a successful job, a threat, a friendly chat) nudge
 * them. The agent brain reads mood through the seam (TScriptInterface<ISeam_MoodProvider>) to weight need
 * urgency, so a stressed townsperson prioritizes Social and a content one relaxes.
 *
 * AUTHORITY & REPLICATION: the server decays axes every frame (sim-clock scaled, reusing the needs clock
 * pattern) and flushes the replicated fast array on MoodReplicationCadence so a crowd is bandwidth-bound.
 * Every mutator (ApplyMoodDelta, SetMood) guards authority at the TOP. Clients observe replicated axes
 * and can still answer the seam reads from their local copy.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_MoodComponent
	: public UActorComponent
	, public ISeam_MoodProvider
{
	GENERATED_BODY()

public:
	USimAg_MoodComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_MoodProvider
	virtual float GetMoodNormalized_Implementation(FGameplayTag MoodTag) const override;
	virtual float GetNeedWeightMultiplier_Implementation(FGameplayTag NeedTag) const override;
	//~ End ISeam_MoodProvider

	/**
	 * Add a signed delta to an axis's Intensity, clamped to [0,1]. AUTHORITY ONLY: early-returns on
	 * clients. No-op if the axis is unknown. @return the new intensity (or the current one on a client).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Mood")
	float ApplyMoodDelta(FGameplayTag Axis, float Delta);

	/** Set an axis's Intensity to an absolute value, clamped to [0,1]. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Mood")
	void SetMood(FGameplayTag Axis, float NewIntensity);

	/** Add (or overwrite the definition of) a mood axis at runtime. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Mood")
	void AddOrUpdateAxis(const FSimAg_Emotion& Emotion);

	/** Current intensity of Axis in [0,1], or its neutral 0.5 if unknown. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Mood")
	float GetIntensity(FGameplayTag Axis) const;

	/** Read-only snapshot of all axes (safe on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Mood")
	TArray<FSimAg_Emotion> GetEmotions() const { return Mood.Emotions; }

	/** Fired after mood values change (server decay / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Mood")
	FSimAg_OnMoodChanged OnMoodChanged;

	/** Called by the fast-array callbacks on clients to surface a replicated mood change. */
	void HandleReplicatedChange();

protected:
	/** Axes this component starts with (authored design-time). Copied into the replicated array on
	 *  BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood")
	TArray<FSimAg_Emotion> DefaultEmotions;

	/**
	 * Designer-authored rules mapping mood axes onto need-urgency multipliers, combined by
	 * GetNeedWeightMultiplier. Several rules for the same need multiply together. No magic numbers in code
	 * — the rules ARE the tuning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood")
	TArray<FSimAg_MoodNeedInfluence> NeedInfluences;

private:
	/** Replicated emotion axes (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_MoodArray Mood;

	/** Real-time accumulator since the last replication flush. */
	float ReplicationAccumulator = 0.f;

	/** Cached replication cadence (seconds) from settings. */
	float ReplicationCadence = 0.5f;

	/** Weak, non-owning handle to the world clock; resolved lazily. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_ClockSubsystem> CachedClock;

	/** Find an axis by tag (mutable). Null if absent. */
	FSimAg_Emotion* FindAxis(const FGameplayTag& Axis);

	/** Find an axis by tag (const). Null if absent. */
	const FSimAg_Emotion* FindAxis(const FGameplayTag& Axis) const;

	/** Server: relax every axis toward its baseline by DeltaSeconds (scaled by the sim clock). */
	void DecayMood(float DeltaSeconds);

	/** Mark an axis dirty for delta replication and fire the local changed delegate + bus. */
	void MarkAxisDirty(FSimAg_Emotion& Emotion, bool bEmitBus);

	/** Resolve the sim clock time-scale (1.0 if no clock; 0 while paused). */
	float GetClockTimeScale() const;

	/** Resolve (and cache) the world clock subsystem. Null-safe. */
	USimAg_ClockSubsystem* GetClock() const;
};
