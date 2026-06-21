// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Score/Seam_ScoreSource.h"
#include "GM_ScoreCarrier.generated.h"

class AGM_ScoreCarrier;

/**
 * One replicated scoreboard row, a fast-array item so individual bucket changes delta-replicate instead of
 * resending the whole board. Mirrors FSeam_ScoreRow's shape (Key/DisplayName/Score) so the carrier can hand
 * the seam-shaped rows straight to ISeam_ScoreSource consumers. All fields are plain replicable value types
 * (no FInstancedStruct), per the net rules.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_ScoreItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Bucket key (team tag / category). The row key; never invalid for a live row. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	FGameplayTag Key;

	/** Display label for the row (from the team config, or derived from the key). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	FText DisplayName;

	/** Current score for this bucket. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	int64 Score = 0;

	FGM_ScoreItem() = default;
	explicit FGM_ScoreItem(const FGameplayTag& InKey) : Key(InKey) {}

	//~ FFastArraySerializerItem replication callbacks (client side only).
	void PreReplicatedRemove(const struct FGM_ScoreArray& InArraySerializer);
	void PostReplicatedAdd(const struct FGM_ScoreArray& InArraySerializer);
	void PostReplicatedChange(const struct FGM_ScoreArray& InArraySerializer);

	/** Project to the seam row type consumers read. */
	FSeam_ScoreRow ToSeamRow() const
	{
		FSeam_ScoreRow Row;
		Row.Key = Key;
		Row.DisplayName = DisplayName;
		Row.Score = Score;
		return Row;
	}
};

/**
 * Fast-array serializer holding the scoreboard rows. NetDeltaSerialize forwards to FastArrayDeltaSerialize
 * so only changed rows cross the wire. The owning-carrier back-pointer is non-replicated and set on both
 * server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_ScoreArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated scoreboard rows. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	TArray<FGM_ScoreItem> Rows;

	/** Non-replicated back-pointer to the owning carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<AGM_ScoreCarrier> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FGM_ScoreItem, FGM_ScoreArray>(Rows, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the score row array. */
template<>
struct TStructOpsTypeTraits<FGM_ScoreArray> : public TStructOpsTypeTraitsBase2<FGM_ScoreArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Fired (server and clients) whenever the scoreboard changes - after replication on clients.
 * @param Carrier The carrier whose scores changed.
 * @param Key     The affected bucket (invalid for board-wide changes such as results being finalised).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGM_OnScoreChanged, AGM_ScoreCarrier*, Carrier, FGameplayTag, Key);

/**
 * Replicated authority carrier for the match scoreboard.
 *
 * The score SUBSYSTEM is never replicated; all authoritative scoreboard state (per-bucket scores and the
 * "results final" flag) lives on THIS AInfo so clients read correct scores. The subsystem spawns exactly
 * one carrier on the server and routes every score mutation through this actor's authority-guarded mutators.
 * The carrier implements ISeam_ScoreSource so any module (HUD, game-flow results, analytics) reads scores
 * through the seam - resolved from the service locator - without depending on the GameMode module.
 *
 * Net dormancy: the carrier sits DORMANT and only flushes dormancy when a score actually changes, so a
 * static scoreboard costs no per-frame bandwidth. The carrier holds NO scoring policy - that lives in the
 * subsystem, which reads this carrier and the ruleset.
 */
UCLASS()
class DESIGNPATTERNSGAMEMODE_API AGM_ScoreCarrier : public AInfo, public ISeam_ScoreSource
{
	GENERATED_BODY()

public:
	AGM_ScoreCarrier();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	// ---- ISeam_ScoreSource (read; safe on clients) ----------------------------------------------
	virtual int64 GetScore_Implementation(FGameplayTag Key) const override;
	virtual void GetAllScores_Implementation(TArray<FSeam_ScoreRow>& OutRows) const override;
	virtual bool AreResultsFinal_Implementation() const override;

	// ---- Authority mutators (each early-returns on clients) -------------------------------------

	/**
	 * Ensure a bucket exists for Key (seeding it with StartingScore and DisplayName). AUTHORITY ONLY.
	 * @return true if a new bucket was created.
	 */
	bool EnsureBucket(const FGameplayTag& Key, int64 StartingScore, const FText& DisplayName);

	/**
	 * Add Delta to Key's bucket (creating it at 0 first if absent). AUTHORITY ONLY.
	 * @return the bucket's new score after the add.
	 */
	int64 AddScore(const FGameplayTag& Key, int64 Delta);

	/** Set Key's bucket to NewScore (creating it if absent). AUTHORITY ONLY. @return the new score. */
	int64 SetScore(const FGameplayTag& Key, int64 NewScore);

	/** Reset every bucket to 0 and clear the results-final flag. AUTHORITY ONLY. */
	void ResetScores();

	/**
	 * Mark results as final (or not). AUTHORITY ONLY. Once true, ISeam_ScoreSource::AreResultsFinal returns
	 * true so a results screen is safe to show.
	 */
	void SetResultsFinal(bool bInFinal);

	// ---- Reads (client-safe) --------------------------------------------------------------------

	/** Find the row for Key, or null. Const, client-safe. */
	const FGM_ScoreItem* FindRow(const FGameplayTag& Key) const;

	/** The bucket key with the highest score, or an empty tag if the board is empty. Client-safe. */
	FGameplayTag GetLeadingKey() const;

	/** Read-only access to the full scoreboard. */
	const TArray<FGM_ScoreItem>& GetRows() const { return Scores.Rows; }

	/** Fired when the scoreboard changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|GameMode|Score")
	FGM_OnScoreChanged OnScoreChanged;

	/** Called by the fast-array item callbacks on clients to surface a replicated score change. */
	void HandleReplicatedScoreChange(const FGameplayTag& Key);

private:
	/** Replicated scoreboard (delta-serialized fast array). */
	UPROPERTY(Replicated)
	FGM_ScoreArray Scores;

	/** Replicated "results are final" flag. OnRep surfaces a board-wide change for results UIs. */
	UPROPERTY(ReplicatedUsing = OnRep_ResultsFinal)
	bool bResultsFinal = false;

	/** Client OnRep for the results flag: surface a board-wide change. */
	UFUNCTION()
	void OnRep_ResultsFinal();

	/** Mutable find for the authority mutators; returns null if absent. */
	FGM_ScoreItem* FindRowMutable(const FGameplayTag& Key);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();
};
