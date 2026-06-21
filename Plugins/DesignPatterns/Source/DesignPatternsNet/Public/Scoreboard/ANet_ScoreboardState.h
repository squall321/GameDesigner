// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Score/Seam_ScoreSource.h"
#include "ANet_ScoreboardState.generated.h"

class ANet_ScoreboardState;

/**
 * One replicated scoreboard row (a fast-array item) for the NET module's spectator/observer scoreboard.
 * Mirrors FSeam_ScoreRow so the carrier hands seam rows straight to ISeam_ScoreSource consumers. This is
 * an ADDITIVE provider: it does not replace the GameMode score carrier; it surfaces match scores through
 * the same seam from the networking layer (e.g. a deathmatch scoreboard the Net lobby/spectator UI reads),
 * so a project without the GameMode module still has a replicated scoreboard.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_ScoreRowItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Bucket key (team tag / player-projected tag / category). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Scoreboard")
	FGameplayTag Key;

	/** Display label. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Scoreboard")
	FText DisplayName;

	/** Current score for this bucket. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Scoreboard")
	int64 Score = 0;

	FNet_ScoreRowItem() = default;
	explicit FNet_ScoreRowItem(const FGameplayTag& InKey) : Key(InKey) {}

	void PreReplicatedRemove(const struct FNet_ScoreRowArray& InArraySerializer);
	void PostReplicatedAdd(const struct FNet_ScoreRowArray& InArraySerializer);
	void PostReplicatedChange(const struct FNet_ScoreRowArray& InArraySerializer);

	FSeam_ScoreRow ToSeamRow() const
	{
		FSeam_ScoreRow Row;
		Row.Key = Key;
		Row.DisplayName = DisplayName;
		Row.Score = Score;
		return Row;
	}
};

/** Fast-array serializer holding the scoreboard rows (delta-replicated). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_ScoreRowArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Scoreboard")
	TArray<FNet_ScoreRowItem> Rows;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ANet_ScoreboardState> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FNet_ScoreRowItem, FNet_ScoreRowArray>(Rows, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FNet_ScoreRowArray> : public TStructOpsTypeTraitsBase2<FNet_ScoreRowArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Fired (server + clients) when the scoreboard changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnScoreboardChanged, FGameplayTag, Key);

/**
 * Replicated authority carrier for the networked scoreboard, implementing the EXISTING ISeam_ScoreSource
 * (additive provider). An AInfo (bReplicates + bAlwaysRelevant), server-spawned and referenced from a
 * replicated GameState UPROPERTY so clients resolve it. Spectators/observers and the lobby results UI read
 * scores through the seam — resolved from the locator under DP.Service.Net.Scoreboard — without depending
 * on this concrete class. Net dormant until a score actually changes.
 *
 * All score MUTATIONS are authority-only. Scoring POLICY lives in the owning game flow; this carrier is a
 * pure replicated store + seam surface.
 */
UCLASS()
class DESIGNPATTERNSNET_API ANet_ScoreboardState : public AInfo, public ISeam_ScoreSource
{
	GENERATED_BODY()

public:
	ANet_ScoreboardState();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	// ---- ISeam_ScoreSource (read; client-safe) ----
	virtual int64 GetScore_Implementation(FGameplayTag Key) const override;
	virtual void GetAllScores_Implementation(TArray<FSeam_ScoreRow>& OutRows) const override;
	virtual bool AreResultsFinal_Implementation() const override;

	// ---- Authority mutators (each early-returns on clients) ----

	/** Ensure a bucket exists for Key. AUTHORITY ONLY. @return true if a new bucket was created. */
	bool EnsureBucket(const FGameplayTag& Key, int64 StartingScore, const FText& DisplayName);

	/** Add Delta to Key's bucket (creating it at 0 if absent). AUTHORITY ONLY. @return the new score. */
	int64 AddScore(const FGameplayTag& Key, int64 Delta);

	/** Set Key's bucket to NewScore. AUTHORITY ONLY. @return the new score. */
	int64 SetScore(const FGameplayTag& Key, int64 NewScore);

	/** Reset all buckets to 0 and clear the results-final flag. AUTHORITY ONLY. */
	void ResetScores();

	/** Mark results final (or not). AUTHORITY ONLY. */
	void SetResultsFinal(bool bInFinal);

	// ---- Reads ----
	const FNet_ScoreRowItem* FindRow(const FGameplayTag& Key) const;
	FGameplayTag GetLeadingKey() const;
	const TArray<FNet_ScoreRowItem>& GetRows() const { return Scores.Rows; }

	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Scoreboard")
	FNet_OnScoreboardChanged OnScoreboardChanged;

	/** Called by fast-array item callbacks on clients to surface a replicated score change. */
	void HandleReplicatedScoreChange();

private:
	void RegisterSelfAsService();
	FNet_ScoreRowItem* FindRowMutable(const FGameplayTag& Key);
	void WakeForChange();

	UPROPERTY(Replicated)
	FNet_ScoreRowArray Scores;

	UPROPERTY(ReplicatedUsing = OnRep_ResultsFinal)
	bool bResultsFinal = false;

	UFUNCTION()
	void OnRep_ResultsFinal();
};
