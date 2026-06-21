// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Query/AI_EnvQuery.h"
#include "AI_QuerySubsystem.generated.h"

class UDP_ServiceLocatorSubsystem;

/**
 * Lightweight EQS-style spatial-query runner: generate -> hard-filter -> score -> normalize -> rank.
 *
 * Built-in scorer is the DEFAULT and needs no AIModule. An OPTIONAL engine-EQS bridge is resolved purely
 * at runtime in the .cpp when a query sets bPreferEngineEQS and supplies a loadable EngineEqsAsset and the
 * engine EQS manager exists — no fake compile macro; the built-in path is always available.
 *
 * NOT replicated. The context/result USTRUCTs are plain value types passed by ref, never networked. The
 * subsystem registers itself under DP.Service.AI.Query (WeakObserved) so other systems resolve it by tag.
 *
 * AUTHORITY: queries are pure reads of world geometry/actors; they run wherever a caller needs a
 * destination (typically the authoritative server when picking AI move targets), but reading does not
 * mutate authoritative state, so there is no authority guard on RunQuery itself.
 */
UCLASS()
class DESIGNPATTERNSAI_API UAI_QuerySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own (true on any non-pure-client net mode).
	 * Exposed for callers that want to gate authoritative use of a query result.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Run Query against Context and fill OutRanked with the valid candidates sorted best-first (highest
	 * score). @return true if at least one valid candidate survived the hard filters.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Query")
	bool RunQuery(UAI_EnvQuery* Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutRanked);

	/** Convenience: run Query and return only the single best candidate. @return true if one was found. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Query")
	bool RunQueryBest(UAI_EnvQuery* Query, const FAI_QueryContext& Context, FAI_ScoredPoint& OutBest);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Self-register under DP.Service.AI.Query (WeakObserved) so consumers resolve us by tag. */
	void RegisterSelfAsService();

	/** Resolve the GameInstance service locator, or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Generate raw candidate points for Query around the context (no scoring yet). */
	void GenerateCandidates(const UAI_EnvQuery& Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutCandidates) const;

	/** Built-in scoring pass: hard-filter, per-test normalize, weighted sum, rank. */
	bool RunBuiltInScorer(const UAI_EnvQuery& Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutRanked) const;

	/**
	 * Try the optional engine-EQS bridge. @return true if the bridge ran AND produced results (so the
	 * built-in scorer is skipped); false to fall back to the built-in scorer. Implemented in the .cpp,
	 * which is the ONLY place AIModule's EQS manager is touched.
	 */
	bool TryRunEngineEqs(const UAI_EnvQuery& Query, const FAI_QueryContext& Context, TArray<FAI_ScoredPoint>& OutRanked) const;

	/** Running count of queries served this session (for the debug string). */
	UPROPERTY(Transient)
	int32 QueriesServed = 0;
};
