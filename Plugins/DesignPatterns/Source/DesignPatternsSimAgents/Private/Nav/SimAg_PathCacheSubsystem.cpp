// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Nav/SimAg_PathCacheSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Core/DPLog.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Engine/World.h"

void USimAg_PathCacheSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		QuantizeCellSize = FMath::Max(1.f, Settings->PathQuantizeCellSize);
		CacheBudget = FMath::Max(1, Settings->PathCacheBudget);
	}
}

void USimAg_PathCacheSubsystem::Deinitialize()
{
	// Drop all pending/cached state. Engine async queries left in flight simply find no waiters when they
	// complete (our callback early-outs on a missing key), so there is no dangling delegate.
	Pending.Reset();
	KeyToNavRequest.Reset();
	Cache.Reset();
	Super::Deinitialize();
}

FIntVector USimAg_PathCacheSubsystem::CellOf(const FVector& Pos) const
{
	const float Safe = FMath::Max(1.f, QuantizeCellSize);
	return FIntVector(
		FMath::FloorToInt(static_cast<float>(Pos.X) / Safe),
		FMath::FloorToInt(static_cast<float>(Pos.Y) / Safe),
		FMath::FloorToInt(static_cast<float>(Pos.Z) / Safe));
}

USimAg_PathCacheSubsystem::FPathKey USimAg_PathCacheSubsystem::MakeKey(const FVector& Start, const FVector& Goal) const
{
	FPathKey Key;
	Key.StartCell = CellOf(Start);
	Key.GoalCell = CellOf(Goal);
	return Key;
}

bool USimAg_PathCacheSubsystem::TryGetCached(const FVector& Start, const FVector& Goal, FNavPathSharedPtr& Out) const
{
	if (const FCacheEntry* Entry = Cache.Find(MakeKey(Start, Goal)))
	{
		if (Entry->Path.IsValid() && Entry->Path->IsValid())
		{
			Out = Entry->Path;
			return true;
		}
	}
	return false;
}

int32 USimAg_PathCacheSubsystem::RequestPathAsync(const FVector& Start, const FVector& Goal, FSimAg_OnPathReady OnReady)
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	if (!Nav)
	{
		return 0;
	}

	const FPathKey Key = MakeKey(Start, Goal);
	const int32 MyRequestId = NextRequestId++;

	// 1) Serve from cache immediately if fresh.
	if (FCacheEntry* Entry = Cache.Find(Key))
	{
		if (Entry->Path.IsValid() && Entry->Path->IsValid())
		{
			Entry->LastUsedSeconds = World->GetTimeSeconds();
			OnReady.ExecuteIfBound(Entry->Path);
			return MyRequestId;
		}
	}

	// 2) Join an in-flight query for the same quantized key.
	if (uint32* NavReq = KeyToNavRequest.Find(Key))
	{
		if (FPendingRequest* InFlight = Pending.Find(*NavReq))
		{
			InFlight->Waiters.Add(TPair<int32, FSimAg_OnPathReady>(MyRequestId, OnReady));
			return MyRequestId;
		}
	}

	// 3) Issue a fresh async query.
	const ANavigationData* NavData = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	if (!NavData)
	{
		return 0;
	}
	FPathFindingQuery Query(this, *NavData, Start, Goal);
	Query.SetAllowPartialPaths(true); // partial paths are useful for crowds: get as close as possible

	FNavPathQueryDelegate Delegate = FNavPathQueryDelegate::CreateUObject(this, &USimAg_PathCacheSubsystem::OnNavPathReady);
	const uint32 NavRequestId = Nav->FindPathAsync(NavData->GetConfig(), Query, Delegate, EPathFindingMode::Regular);
	if (NavRequestId == INVALID_NAVQUERYID)
	{
		return 0;
	}

	FPendingRequest Request;
	Request.Key = Key;
	Request.RepresentativeStart = Start;
	Request.RepresentativeGoal = Goal;
	Request.Waiters.Add(TPair<int32, FSimAg_OnPathReady>(MyRequestId, OnReady));
	Pending.Add(NavRequestId, MoveTemp(Request));
	KeyToNavRequest.Add(Key, NavRequestId);
	return MyRequestId;
}

void USimAg_PathCacheSubsystem::CancelRequest(int32 RequestId)
{
	for (TPair<uint32, FPendingRequest>& Pair : Pending)
	{
		Pair.Value.Waiters.RemoveAll([RequestId](const TPair<int32, FSimAg_OnPathReady>& W) { return W.Key == RequestId; });
	}
}

void USimAg_PathCacheSubsystem::OnNavPathReady(uint32 NavRequestId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
{
	FPendingRequest Request;
	if (!Pending.RemoveAndCopyValue(NavRequestId, Request))
	{
		return; // request was cancelled / subsystem torn down
	}
	KeyToNavRequest.Remove(Request.Key);

	// Cache successful (or partial) paths so coalesced/later requests reuse them.
	const bool bUsable = (Result == ENavigationQueryResult::Success) && Path.IsValid() && Path->IsValid();
	if (bUsable)
	{
		StorePath(Request.Key, Path);
	}

	// Fan the result out to every joined waiter.
	for (const TPair<int32, FSimAg_OnPathReady>& Waiter : Request.Waiters)
	{
		Waiter.Value.ExecuteIfBound(Path);
	}
}

void USimAg_PathCacheSubsystem::StorePath(const FPathKey& Key, const FNavPathSharedPtr& Path)
{
	UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	FCacheEntry& Entry = Cache.FindOrAdd(Key);
	Entry.Path = Path;
	Entry.LastUsedSeconds = Now;

	// LRU eviction past budget.
	while (Cache.Num() > CacheBudget)
	{
		const FPathKey* Oldest = nullptr;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<FPathKey, FCacheEntry>& Pair : Cache)
		{
			if (Pair.Value.LastUsedSeconds < OldestTime)
			{
				OldestTime = Pair.Value.LastUsedSeconds;
				Oldest = &Pair.Key;
			}
		}
		if (!Oldest)
		{
			break;
		}
		Cache.Remove(*Oldest);
	}
}

FString USimAg_PathCacheSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimAg PathCache: cached=%d pending=%d budget=%d cell=%.0f"),
		Cache.Num(), Pending.Num(), CacheBudget, QuantizeCellSize);
}
