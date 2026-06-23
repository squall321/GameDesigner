// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "AI/Navigation/NavigationTypes.h"
#include "SimAg_PathCacheSubsystem.generated.h"

/**
 * Delegate fired when an async path request completes. Carries the resolved path (may be partial or
 * invalid — check IsValid / IsPartial on the shared pointer's data).
 */
DECLARE_DELEGATE_OneParam(FSimAg_OnPathReady, FNavPathSharedPtr /*Path*/);

/**
 * World-scoped async path hub + cache. It coalesces duplicate crowd path requests (many agents heading to
 * nearly the same place share ONE query, keyed by quantized start/goal), hands back cached paths
 * immediately when available, and evicts least-recently-used entries past PathCacheBudget.
 *
 * Wraps the engine UNavigationSystemV1 (FindPathAsync) — it does NOT reimplement pathfinding. Transient
 * only; never replicated. Quantization cell size + cache budget come from settings (no magic numbers).
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_PathCacheSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Request a path from Start to Goal. If a fresh cached path for the quantized (Start, Goal) exists it
	 * is returned synchronously via OnReady this frame; otherwise an async nav query is issued (or joined
	 * if one is already in flight for the same quantized key) and OnReady fires on completion.
	 * @return a request id (>0) you may use to cancel, or 0 if the request couldn't be started.
	 */
	int32 RequestPathAsync(const FVector& Start, const FVector& Goal, FSimAg_OnPathReady OnReady);

	/**
	 * Try to read a cached path for the quantized (Start, Goal) without issuing a query. @return true and
	 * fills Out when a fresh cached path exists.
	 */
	bool TryGetCached(const FVector& Start, const FVector& Goal, FNavPathSharedPtr& Out) const;

	/** Cancel a pending request by id (its delegate will not fire). Cached entries are unaffected. */
	void CancelRequest(int32 RequestId);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** A quantized path key: integer cells for start and goal so near-identical requests collide. */
	struct FPathKey
	{
		FIntVector StartCell = FIntVector::ZeroValue;
		FIntVector GoalCell = FIntVector::ZeroValue;

		bool operator==(const FPathKey& O) const { return StartCell == O.StartCell && GoalCell == O.GoalCell; }
		friend uint32 GetTypeHash(const FPathKey& K)
		{
			return HashCombine(GetTypeHash(K.StartCell), GetTypeHash(K.GoalCell));
		}
	};

	/** One cached, resolved path plus its last-use stamp for LRU eviction. */
	struct FCacheEntry
	{
		FNavPathSharedPtr Path;
		double LastUsedSeconds = 0.0;
	};

	/** One in-flight request key's joined waiters. */
	struct FPendingRequest
	{
		FPathKey Key;
		TArray<TPair<int32, FSimAg_OnPathReady>> Waiters; // (requestId, callback)
		FVector RepresentativeStart = FVector::ZeroVector;
		FVector RepresentativeGoal = FVector::ZeroVector;
	};

	/** Resolved-path cache, keyed by quantized start/goal. */
	TMap<FPathKey, FCacheEntry> Cache;

	/** In-flight nav queries keyed by quantized start/goal, each with its joined waiters. */
	TMap<uint32 /*navRequestId*/, FPendingRequest> Pending;

	/** Reverse index: quantized key -> nav request id, so a new request can join an in-flight one. */
	TMap<FPathKey, uint32> KeyToNavRequest;

	/** Monotonic id source for our own (caller-facing) request ids. */
	int32 NextRequestId = 1;

	/** Cell size (world units) for quantization. Cached from settings. */
	float QuantizeCellSize = 200.f;

	/** Max cached entries before LRU eviction. Cached from settings. */
	int32 CacheBudget = 256;

	/** Quantize a world position to an integer cell. */
	FIntVector CellOf(const FVector& Pos) const;

	/** Build the quantized key for a start/goal pair. */
	FPathKey MakeKey(const FVector& Start, const FVector& Goal) const;

	/** Insert/refresh a resolved path in the cache and evict LRU if over budget. */
	void StorePath(const FPathKey& Key, const FNavPathSharedPtr& Path);

	/** Engine async path completion callback (matches FNavPathQueryDelegate's signature). */
	void OnNavPathReady(uint32 NavRequestId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path);
};
