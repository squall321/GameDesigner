// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "GM_ScoreSubsystem.generated.h"

class AGM_ScoreCarrier;
class UGM_RulesetDefinition;
class UDP_ServiceLocatorSubsystem;

/**
 * World-scoped authority for match scoring.
 *
 * The subsystem holds NO replicated state itself (per the net rules): it spawns and owns the lifecycle of a
 * single replicated AGM_ScoreCarrier on the authority, and routes every authoritative AddScore/SetScore
 * through that carrier's guarded mutators. Clients read scores through the carrier's ISeam_ScoreSource seam
 * (registered with the service locator under DP.Service.GM.Score), never through this subsystem.
 *
 * Responsibilities:
 *   - Spawn the score carrier (eagerly or lazily per settings) once the world has a GameState authority.
 *   - Seed scoreboard buckets from the active ruleset's team config at match start.
 *   - Apply authoritative score changes and, after each change, ask the match component to re-evaluate
 *     win conditions so a scoring play that crosses a threshold ends the match immediately.
 *   - Finalise results (locking the carrier's results-final flag) when the match reaches MatchOver.
 *
 * Cross-module: this subsystem couples to other systems ONLY via the score seam and the message bus; the
 * match component is a sibling within this module, resolved off the GameState by component lookup.
 */
UCLASS()
class DESIGNPATTERNSGAMEMODE_API UGM_ScoreSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone /
	 * listen-server host (any net mode that is not a pure client). All score WRITES gate on this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- Carrier lifecycle ----------------------------------------------------------------------

	/**
	 * Resolve the score carrier, spawning it on the authority if it does not yet exist. On clients this
	 * returns the replicated carrier once it has arrived (null until then). @return the carrier, or null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	AGM_ScoreCarrier* GetOrSpawnCarrier();

	/** The current carrier without spawning one (client-safe; null until replicated/spawned). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Score")
	AGM_ScoreCarrier* GetCarrier() const;

	// ---- Match wiring ---------------------------------------------------------------------------

	/**
	 * Seed the scoreboard from Ruleset's team config (one bucket per team, at its StartingScore). AUTHORITY
	 * ONLY. Call at match start. A null ruleset seeds nothing (ad-hoc buckets are created on first score).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	void SeedFromRuleset(UGM_RulesetDefinition* Ruleset);

	/**
	 * Finalise results: lock the carrier's results-final flag and broadcast the decided message. AUTHORITY
	 * ONLY. Called by the match component on entering MatchOver. WinningKey may be empty (a draw).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	void FinalizeResults(FGameplayTag WinningKey);

	// ---- Authority score mutators (each early-returns on clients) -------------------------------

	/**
	 * Add Delta to a bucket. AUTHORITY ONLY. An invalid Key routes to the settings default bucket. After
	 * applying, re-evaluates the match's win conditions. @return the bucket's new score (0 off authority).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	int64 AddScore(FGameplayTag Key, int64 Delta);

	/** Set a bucket to NewScore. AUTHORITY ONLY. @return the new score (0 off authority). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	int64 SetScore(FGameplayTag Key, int64 NewScore);

	/** Reset every bucket to 0 and clear the results-final flag. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Score")
	void ResetScores();

	// ---- Reads (client-safe; delegate to the carrier/seam) --------------------------------------

	/** Score for Key (0 if absent / no carrier). Reads through the carrier. Client-safe. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Score")
	int64 GetScore(FGameplayTag Key) const;

	/** The bucket key currently leading (empty if no carrier / empty board). Client-safe. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Score")
	FGameplayTag GetLeadingKey() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Weak handle to the spawned/replicated carrier. The carrier is owned by the world (not this
	 * subsystem); held weakly and re-resolved from the world if it drops. Null-checked everywhere.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AGM_ScoreCarrier> CarrierWeak;

	/** Default score bucket from settings (resolved once at init; falls back to DP.Score.Default). */
	FGameplayTag DefaultBucket;

	/** Spawn the carrier on the authority (no-op on clients / if one already exists). */
	AGM_ScoreCarrier* SpawnCarrierIfAuthority();

	/** Find an already-live carrier in the world (so clients pick up the replicated one). */
	AGM_ScoreCarrier* FindCarrierInWorld() const;

	/** Register the carrier's ISeam_ScoreSource under DP.Service.GM.Score (WeakObserved). */
	void RegisterCarrierService(AGM_ScoreCarrier* Carrier);

	/** Resolve the service locator (GameInstance-scoped), or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Resolve the GameState's match component (sibling, looked up by component), or null. */
	UGM_MatchStateComponent* FindMatchComponent() const;

	/** Normalise a possibly-invalid key to the configured default bucket. */
	FGameplayTag NormalizeKey(const FGameplayTag& Key) const;

	/** Broadcast a score-changed message on the bus (best-effort; null bus is a no-op). */
	void PublishScoreChanged(const FGameplayTag& Key, int64 NewScore, int64 Delta) const;
};

// Forward-declared above; full type pulled in by the .cpp.
class UGM_MatchStateComponent;
