// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Needs/Seam_NeedProvider.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "SimAg_NeedsComponent.generated.h"

class USimAg_NeedsComponent;
struct FSimAg_NeedsArray;

/**
 * One generalized need meter: a tag-keyed value that drains over time. Current/Max define the raw
 * range; the normalized satisfaction reported to the seam is Current/Max in [0,1].
 *
 * This is a FFastArraySerializerItem so individual meters delta-replicate (a single drained need
 * doesn't resend the whole set). It is genre-neutral — hunger, social, fun, hygiene, energy are all
 * just tags under SimAgNativeTags::Need.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_NeedMeter : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Identity of the need (child of SimAg.Need). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs", meta = (Categories = "SimAg.Need"))
	FGameplayTag Need;

	/** Current value, clamped to [0, Max]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs", meta = (ClampMin = "0.0"))
	float Current = 100.f;

	/** Maximum value (full satisfaction). Must be > 0 for a meaningful normalized value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs", meta = (ClampMin = "0.01"))
	float Max = 100.f;

	/**
	 * Units drained from Current per real second of authoritative simulation (scaled by the sim
	 * clock's time scale where one is available). A positive value depletes the need over time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs", meta = (ClampMin = "0.0"))
	float DrainPerSecond = 1.f;

	/**
	 * Normalized [0,1] threshold below which this need is "critical" (fires OnNeedCritical on the
	 * downward edge). Authored per-need so e.g. Energy can be critical earlier than Fun.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalThreshold = 0.2f;

	FSimAg_NeedMeter() = default;

	/** Normalized satisfaction in [0,1]; 0 if Max is non-positive. */
	float GetNormalized() const { return Max > 0.f ? FMath::Clamp(Current / Max, 0.f, 1.f) : 0.f; }

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_NeedsArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_NeedsArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_NeedsArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the agent's need meters. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed meters cross the wire on each replication flush.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_NeedsArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated meters. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Needs")
	TArray<FSimAg_NeedMeter> Meters;

	/** Non-replicated back-pointer to the owning component, for change notifications on clients. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimAg_NeedsComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_NeedMeter, FSimAg_NeedsArray>(Meters, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the needs array. */
template<>
struct TStructOpsTypeTraits<FSimAg_NeedsArray> : public TStructOpsTypeTraitsBase2<FSimAg_NeedsArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Message-bus payload broadcast when a need goes critical. Carried as an FInstancedStruct through
 * UDP_MessageBusSubsystem on channel SimAgNativeTags::Bus_NeedCritical.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_NeedEvent
{
	GENERATED_BODY()

	/** Which need went critical. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Needs")
	FGameplayTag Need;

	/** Normalized value at the moment it crossed the threshold. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Needs")
	float Normalized = 0.f;
};

/** Fired (server and clients) when a need crosses below its critical threshold (downward edge). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnNeedCritical, FGameplayTag, Need, float, Normalized);

/** Fired (server and clients) after any meter value changes (drain on server, replication on clients). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnNeedsChanged, USimAg_NeedsComponent*, NeedsComponent);

/**
 * Generalized, replicated needs container implementing the shared ISeam_NeedProvider seam. The agent
 * brain composes this with other providers (e.g. a Survival needs adapter) — it asks SupportsNeed and
 * reads GetNeedNormalized only from the provider that owns each tag.
 *
 * AUTHORITY & REPLICATION: the server drains meters every frame and flushes the replicated array on a
 * THROTTLED cadence (NeedsReplicationCadence from settings) so a crowd of agents costs bounded
 * bandwidth. Meters delta-replicate via a FFastArraySerializer. Every mutator (ApplyNeedDelta,
 * SetNeed*, AddOrUpdateNeed) guards authority at the top and early-returns on clients; clients only
 * observe replicated values. OnNeedCritical fires on the exact downward edge, deduped per need so it
 * doesn't re-fire every frame while a need sits below threshold.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_NeedsComponent : public UActorComponent, public ISeam_NeedProvider
{
	GENERATED_BODY()

public:
	USimAg_NeedsComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_NeedProvider
	virtual float GetNeedNormalized_Implementation(FGameplayTag NeedTag) const override;
	virtual bool SupportsNeed_Implementation(FGameplayTag NeedTag) const override;
	virtual void GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const override;
	//~ End ISeam_NeedProvider

	/**
	 * Apply a signed delta to NeedTag's Current value (positive replenishes, negative depletes),
	 * clamped to [0, Max]. AUTHORITY ONLY: early-returns on clients. No-op if the need is unknown.
	 * Returns the new normalized value (or the current one on a client / unknown need).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Needs")
	float ApplyNeedDelta(FGameplayTag NeedTag, float Delta);

	/**
	 * Set NeedTag's Current to an absolute value, clamped to [0, Max]. AUTHORITY ONLY. No-op if the
	 * need is unknown.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Needs")
	void SetNeedValue(FGameplayTag NeedTag, float NewValue);

	/**
	 * Add a need (or overwrite its definition if it already exists). AUTHORITY ONLY. Use to register
	 * needs at runtime; design-time needs are authored in DefaultNeeds.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Needs")
	void AddOrUpdateNeed(const FSimAg_NeedMeter& Meter);

	/** True if NeedTag's normalized value is at/below its critical threshold right now. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Needs")
	bool IsNeedCritical(FGameplayTag NeedTag) const;

	/** Read-only snapshot of all meters (safe on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Needs")
	TArray<FSimAg_NeedMeter> GetMeters() const { return Needs.Meters; }

	/** Fired when a need crosses below its critical threshold. */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Needs")
	FSimAg_OnNeedCritical OnNeedCritical;

	/** Fired after meter values change (server drain / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Needs")
	FSimAg_OnNeedsChanged OnNeedsChanged;

	/** Called by the fast-array meter callbacks on clients to surface a value change. */
	void HandleReplicatedChange();

protected:
	/**
	 * Needs this component starts with. Authored design-time (genre-neutral; any tag under SimAg.Need).
	 * Copied into the replicated array on BeginPlay (authority only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Needs")
	TArray<FSimAg_NeedMeter> DefaultNeeds;

private:
	/** Replicated meters (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_NeedsArray Needs;

	/** Real-time accumulator since the last replication flush, compared against the settings cadence. */
	float ReplicationAccumulator = 0.f;

	/** Cached replication cadence (seconds) from settings, refreshed at BeginPlay. */
	float ReplicationCadence = 0.5f;

	/**
	 * Per-need "currently below critical" flags, so OnNeedCritical fires only on the DOWNWARD edge and
	 * not every frame while a need stays low. Keyed by need tag; transient (server-side bookkeeping).
	 */
	TMap<FGameplayTag, bool> CriticalState;

	/** Find a meter by tag (mutable). Null if absent. */
	FSimAg_NeedMeter* FindMeter(const FGameplayTag& NeedTag);

	/** Find a meter by tag (const). Null if absent. */
	const FSimAg_NeedMeter* FindMeter(const FGameplayTag& NeedTag) const;

	/** Server: advance drains by DeltaSeconds (scaled by the sim clock where available). */
	void DrainNeeds(float DeltaSeconds);

	/** Mark a meter dirty for delta replication and fire the local changed delegate. */
	void MarkMeterDirty(FSimAg_NeedMeter& Meter);

	/** Evaluate critical edges for every meter and fire OnNeedCritical / bus on downward crossings. */
	void EvaluateCriticalEdges();

	/** Resolve the sim clock time-scale (1.0 if no clock), so drains honour pause / speed. */
	float GetClockTimeScale() const;
};
