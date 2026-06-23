// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "SimAg_MoodArray.generated.h"

class USimAg_MoodComponent;
struct FSimAg_MoodArray;

/**
 * One mood/emotion axis: an Intensity in [0,1] that decays toward a per-axis Baseline over time. Axis is
 * a tag under SimAgNativeTags::Mood (e.g. SimAg.Mood.Happiness), so any genre defines its own emotions.
 *
 * Fast-array item so individual axes delta-replicate (one shifting emotion doesn't resend the whole mood
 * set), mirroring FSimAg_NeedMeter. All members are plain replicable types — no FInstancedStruct.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_Emotion : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Identity of the axis (child of SimAg.Mood). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (Categories = "SimAg.Mood"))
	FGameplayTag Axis;

	/** Current intensity in [0,1]. 0.5 is the conventional neutral mid-point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Intensity = 0.5f;

	/** Resting value this axis decays toward when nothing perturbs it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Baseline = 0.5f;

	/**
	 * Units of Intensity that relax toward Baseline per SECOND of authoritative sim time (scaled by the
	 * sim clock). A positive value pulls the axis home; 0 means it holds whatever it was set to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (ClampMin = "0.0"))
	float DecayPerSecond = 0.05f;

	FSimAg_Emotion() = default;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_MoodArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_MoodArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_MoodArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the agent's emotion axes. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize. Exact mirror of FSimAg_NeedsArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MoodArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated emotion axes. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Mood")
	TArray<FSimAg_Emotion> Emotions;

	/** Non-replicated back-pointer to the owning component, for change notifications on clients. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimAg_MoodComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_Emotion, FSimAg_MoodArray>(Emotions, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the mood array. */
template<>
struct TStructOpsTypeTraits<FSimAg_MoodArray> : public TStructOpsTypeTraitsBase2<FSimAg_MoodArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Designer-authored rule: how an emotion axis modulates the URGENCY weight of a need. A stressed agent
 * over-weights Social; a content agent under-weights Fun. The mood component combines all matching rules
 * into the multiplier returned by ISeam_MoodProvider::GetNeedWeightMultiplier.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MoodNeedInfluence
{
	GENERATED_BODY()

	/** Mood axis whose intensity drives the influence (child of SimAg.Mood). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (Categories = "SimAg.Mood"))
	FGameplayTag Axis;

	/** Need whose urgency this rule scales (child of SimAg.Need). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (Categories = "SimAg.Need"))
	FGameplayTag Need;

	/**
	 * Multiplier applied to the need's urgency when the axis is at full intensity (1.0). The actual
	 * applied factor lerps from 1.0 (no influence) at Intensity 0 to this value at Intensity 1, so a
	 * value >1 amplifies the need with the emotion and <1 suppresses it. A pure designer weight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Mood", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float WeightAtFullIntensity = 1.5f;
};
