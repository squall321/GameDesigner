// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetSession.h"
#include "Flow_MatchmakingController.generated.h"

class UFlow_GameFlowSubsystem;
class UDP_MessageBusSubsystem;

/**
 * Drives matchmaking / session flow on behalf of the flow subsystem, entirely through the shared
 * ISeam_NetSession seam (search / create / join / leave) plus ISeam_LobbyRead for ready-state. Owned as a
 * UPROPERTY(Transient) subobject of UFlow_GameFlowSubsystem (NewObject(Outer)) so it is GameInstance-
 * lifetime and survives travel with the flow.
 *
 * Behaviour (all tunables are data — see UFlow_DeveloperSettings):
 *  - Quick-match: search, then join the best result or host if none found.
 *  - Explicit host / join-by-index requests.
 *  - Data-driven EXPONENTIAL RETRY/BACKOFF on a transient failure: on a Failed phase it re-attempts up to
 *    MatchmakingMaxRetries with delay RetryBaseSeconds * RetryBackoffMultiplier^attempt (timer-driven, no
 *    ticking). On terminal failure (retries exhausted / online unavailable) it forces the flow into the
 *    configured NetErrorPhase via the subsystem's existing ForceTransition and broadcasts on the bus.
 *  - Polls the net-session phase off a low-frequency timer (the seam exposes a phase, not a delegate) and
 *    advances Lobby -> Loading by RE-PUBLISHING readiness through the flow once ISeam_LobbyRead reports
 *    all players ready.
 *
 * Holds the seam WEAKLY (TWeakInterfacePtr) and RE-RESOLVES through the locator when stale — the Net
 * adapter is owned elsewhere and must never be kept alive by this controller.
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_MatchmakingController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind this controller to its owning flow subsystem (called once right after construction). */
	void Initialize(UFlow_GameFlowSubsystem* InOwner);

	/** Release timers / weak refs (called from the owner's Deinitialize). */
	void Shutdown();

	/** Quick-match: search with Query, then auto-join the best result or host if none are found. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void RequestQuickMatch(const FSeam_SessionQuery& Query);

	/** Explicitly host a session described by Desc. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void RequestHost(const FSeam_SessionDesc& Desc);

	/** Search for sessions (results read via GetLastResults once the search completes). */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void RequestSearch(const FSeam_SessionQuery& Query);

	/** Join a previously-found session by its result index. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void RequestJoinByIndex(int32 ResultIndex);

	/** Cancel any in-flight matchmaking, stop retries, and leave the session. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void CancelMatchmaking();

	/** The current matchmaking phase as a tag (DP.Flow.Matchmaking.Phase.*), derived from the seam phase. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Matchmaking")
	FGameplayTag GetMatchmakingPhase() const;

	/** The most recent search results (copied from the seam). */
	UFUNCTION(BlueprintCallable, Category = "Flow|Matchmaking")
	void GetLastResults(TArray<FSeam_SessionResult>& OutResults) const;

private:
	/** Intent the controller is pursuing, so phase polling knows what to do when a sub-step completes. */
	enum class EIntent : uint8
	{
		None,
		QuickMatch,   // search -> join best / host
		Search,       // search only
		Host,         // create only
		Join          // join a specific index
	};

	/** Resolve (re-resolving if stale) the ISeam_NetSession provider object, or null. */
	UObject* ResolveSession() const;

	/** Resolve the ISeam_LobbyRead provider object, or null. */
	UObject* ResolveLobby() const;

	/** Resolve the owning GameInstance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Begin polling the seam phase on the low-frequency timer (idempotent). */
	void StartPolling();

	/** Stop the phase-poll timer. */
	void StopPolling();

	/** Timer callback: read the seam phase, drive the active intent, detect completion/failure. */
	void PollSessionPhase();

	/** Handle a Failed seam phase: schedule a backoff retry, or terminal-fail into NetErrorPhase. */
	void HandleFailure();

	/** Timer callback firing one scheduled retry of the current intent. */
	void FireRetry();

	/** Re-issue the current intent's initiating seam call (used by FireRetry). */
	void ReissueIntent();

	/** Map an ESeam_NetSessionPhase to a matchmaking phase tag. */
	static FGameplayTag PhaseToTag(ESeam_NetSessionPhase Phase);

	/** Broadcast the current matchmaking state on DP.Bus.Flow.MatchmakingChanged. */
	void BroadcastState(bool bTerminalFailure);

	/** Terminal-fail: force the flow into NetErrorPhase and announce it. */
	void EnterNetError();

	// --- State ---

	/** The owning flow subsystem (GameInstance-lifetime; this object is its subobject). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlow_GameFlowSubsystem> Owner;

	/** Weakly-held resolved net-session seam (re-resolved when stale). */
	TWeakInterfacePtr<ISeam_NetSession> CachedSession;

	/** The intent currently being pursued. */
	EIntent ActiveIntent = EIntent::None;

	/** Stored query/desc/index for the active intent (so a retry re-issues the same request). */
	FSeam_SessionQuery PendingQuery;
	FSeam_SessionDesc PendingDesc;
	int32 PendingJoinIndex = INDEX_NONE;

	/** The last seam phase observed by the poller (to detect edges). */
	ESeam_NetSessionPhase LastObservedPhase = ESeam_NetSessionPhase::Idle;

	/** Current retry attempt count for the active intent. */
	int32 RetryCount = 0;

	/** Timer handle for the phase poll. */
	FTimerHandle PollTimer;

	/** Timer handle for a scheduled backoff retry. */
	FTimerHandle RetryTimer;

	/** True once we have advanced Lobby -> Loading for the current session (so we do it once). */
	bool bAdvancedFromLobby = false;
};
