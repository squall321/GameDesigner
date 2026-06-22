// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Matchmaking/Flow_MatchmakingController.h"
#include "Flow/Flow_GameFlowSubsystem.h"
#include "Flow/Flow_OrchestratorTypes.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Net/Seam_LobbyRead.h"

#include "Engine/World.h"
#include "TimerManager.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_MatchmakingController::Initialize(UFlow_GameFlowSubsystem* InOwner)
{
	Owner = InOwner;
}

void UFlow_MatchmakingController::Shutdown()
{
	StopPolling();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}
	CachedSession.Reset();
	ActiveIntent = EIntent::None;
	Owner.Reset();
}

// ---------------------------------------------------------------------------------------------------
// Public requests
// ---------------------------------------------------------------------------------------------------

void UFlow_MatchmakingController::RequestQuickMatch(const FSeam_SessionQuery& Query)
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][MM] QuickMatch with no ISeam_NetSession provider; terminal-failing."));
		EnterNetError();
		return;
	}

	ActiveIntent = EIntent::QuickMatch;
	PendingQuery = Query;
	RetryCount = 0;
	bAdvancedFromLobby = false;
	LastObservedPhase = ESeam_NetSessionPhase::Idle;

	ISeam_NetSession::Execute_FindSessions(Session, Query);
	StartPolling();
	BroadcastState(/*bTerminalFailure*/ false);
}

void UFlow_MatchmakingController::RequestHost(const FSeam_SessionDesc& Desc)
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][MM] Host with no ISeam_NetSession provider; terminal-failing."));
		EnterNetError();
		return;
	}

	ActiveIntent = EIntent::Host;
	PendingDesc = Desc;
	RetryCount = 0;
	bAdvancedFromLobby = false;
	LastObservedPhase = ESeam_NetSessionPhase::Idle;

	ISeam_NetSession::Execute_CreateSession(Session, Desc);
	StartPolling();
	BroadcastState(false);
}

void UFlow_MatchmakingController::RequestSearch(const FSeam_SessionQuery& Query)
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][MM] Search with no ISeam_NetSession provider; terminal-failing."));
		EnterNetError();
		return;
	}

	ActiveIntent = EIntent::Search;
	PendingQuery = Query;
	RetryCount = 0;
	LastObservedPhase = ESeam_NetSessionPhase::Idle;

	ISeam_NetSession::Execute_FindSessions(Session, Query);
	StartPolling();
	BroadcastState(false);
}

void UFlow_MatchmakingController::RequestJoinByIndex(int32 ResultIndex)
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][MM] Join with no ISeam_NetSession provider; terminal-failing."));
		EnterNetError();
		return;
	}

	ActiveIntent = EIntent::Join;
	PendingJoinIndex = ResultIndex;
	RetryCount = 0;
	bAdvancedFromLobby = false;
	LastObservedPhase = ESeam_NetSessionPhase::Idle;

	ISeam_NetSession::Execute_JoinSession(Session, ResultIndex);
	StartPolling();
	BroadcastState(false);
}

void UFlow_MatchmakingController::CancelMatchmaking()
{
	StopPolling();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}

	if (UObject* Session = ResolveSession())
	{
		ISeam_NetSession::Execute_LeaveSession(Session);
	}

	ActiveIntent = EIntent::None;
	RetryCount = 0;
	LastObservedPhase = ESeam_NetSessionPhase::Idle;
	BroadcastState(false);
}

// ---------------------------------------------------------------------------------------------------
// Phase polling + intent driving
// ---------------------------------------------------------------------------------------------------

void UFlow_MatchmakingController::StartPolling()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (World->GetTimerManager().IsTimerActive(PollTimer))
	{
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const float Interval = Settings ? FMath::Max(0.05f, Settings->SessionPollIntervalSeconds) : 0.5f;

	World->GetTimerManager().SetTimer(PollTimer, FTimerDelegate::CreateUObject(this, &UFlow_MatchmakingController::PollSessionPhase),
		Interval, /*bLoop*/ true);
}

void UFlow_MatchmakingController::StopPolling()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
}

void UFlow_MatchmakingController::PollSessionPhase()
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		// Provider vanished mid-flight (e.g. world torn down): terminal-fail and stop.
		StopPolling();
		EnterNetError();
		return;
	}

	const ESeam_NetSessionPhase Phase = ISeam_NetSession::Execute_GetSessionPhase(Session);
	const bool bChanged = (Phase != LastObservedPhase);
	LastObservedPhase = Phase;

	switch (Phase)
	{
	case ESeam_NetSessionPhase::Failed:
		HandleFailure();
		return;

	case ESeam_NetSessionPhase::Idle:
	{
		// A search that returned to Idle has completed. For QuickMatch, decide join-best vs host.
		if (ActiveIntent == EIntent::QuickMatch)
		{
			TArray<FSeam_SessionResult> Results;
			ISeam_NetSession::Execute_GetSearchResults(Session, Results);

			if (Results.Num() > 0)
			{
				// Join the first result (the adapter returns results best-first / by ping).
				PendingJoinIndex = Results[0].ResultIndex;
				ISeam_NetSession::Execute_JoinSession(Session, PendingJoinIndex);
			}
			else
			{
				// None found: host instead, seeding the host desc from the query.
				FSeam_SessionDesc Desc;
				Desc.bLAN = PendingQuery.bLAN;
				ISeam_NetSession::Execute_CreateSession(Session, Desc);
			}
			if (bChanged)
			{
				BroadcastState(false);
			}
		}
		else if (ActiveIntent == EIntent::Search && bChanged)
		{
			// Search-only completed: announce results count, then stop polling.
			BroadcastState(false);
			StopPolling();
			ActiveIntent = EIntent::None;
		}
		return;
	}

	case ESeam_NetSessionPhase::InSession:
	{
		// Connected. Re-publish readiness so the existing flow can advance Lobby -> Loading.
		if (!bAdvancedFromLobby)
		{
			UObject* Lobby = ResolveLobby();
			const bool bAllReady = Lobby && Lobby->Implements<USeam_LobbyRead>()
				? ISeam_LobbyRead::Execute_AreAllPlayersReady(Lobby)
				: true; // No lobby seam: nothing gates the start, so treat as ready.

			if (UFlow_GameFlowSubsystem* OwningFlow = Owner.Get())
			{
				if (bAllReady && OwningFlow->GetCurrentPhase() == FlowTags::Phase_Lobby)
				{
					OwningFlow->RequestTransition_Implementation(FlowTags::Phase_Loading);
					bAdvancedFromLobby = true;
				}
			}
		}
		if (bChanged)
		{
			RetryCount = 0; // Success clears the retry budget.
			BroadcastState(false);
		}
		return;
	}

	default:
		// Searching / Creating / Joining: in-flight, just mirror the phase on a change.
		if (bChanged)
		{
			BroadcastState(false);
		}
		return;
	}
}

void UFlow_MatchmakingController::HandleFailure()
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const int32 MaxRetries = Settings ? Settings->MatchmakingMaxRetries : 3;

	if (RetryCount >= MaxRetries)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][MM] Matchmaking failed after %d retries; terminal NetError."), RetryCount);
		StopPolling();
		EnterNetError();
		return;
	}

	// Schedule a backoff retry: delay = Base * Mult^attempt.
	const float Base = Settings ? FMath::Max(0.f, Settings->RetryBaseSeconds) : 2.f;
	const float Mult = Settings ? FMath::Max(1.f, Settings->RetryBackoffMultiplier) : 2.f;
	const float Delay = Base * FMath::Pow(Mult, static_cast<float>(RetryCount));

	++RetryCount;
	BroadcastState(/*bTerminalFailure*/ false);

	UWorld* World = GetWorld();
	if (!World)
	{
		EnterNetError();
		return;
	}

	UE_LOG(LogDP, Log, TEXT("[Flow][MM] Matchmaking failure; retry %d in %.1fs."), RetryCount, Delay);
	// Pause polling until the retry fires (so we don't re-trigger HandleFailure on the still-Failed phase).
	StopPolling();
	World->GetTimerManager().SetTimer(RetryTimer, FTimerDelegate::CreateUObject(this, &UFlow_MatchmakingController::FireRetry),
		FMath::Max(0.01f, Delay), /*bLoop*/ false);
}

void UFlow_MatchmakingController::FireRetry()
{
	LastObservedPhase = ESeam_NetSessionPhase::Idle;
	ReissueIntent();
	StartPolling();
}

void UFlow_MatchmakingController::ReissueIntent()
{
	UObject* Session = ResolveSession();
	if (!Session)
	{
		EnterNetError();
		return;
	}

	switch (ActiveIntent)
	{
	case EIntent::QuickMatch:
	case EIntent::Search:
		ISeam_NetSession::Execute_FindSessions(Session, PendingQuery);
		break;
	case EIntent::Host:
		ISeam_NetSession::Execute_CreateSession(Session, PendingDesc);
		break;
	case EIntent::Join:
		ISeam_NetSession::Execute_JoinSession(Session, PendingJoinIndex);
		break;
	default:
		break;
	}
}

void UFlow_MatchmakingController::EnterNetError()
{
	ActiveIntent = EIntent::None;
	BroadcastState(/*bTerminalFailure*/ true);

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const FGameplayTag ErrorPhase = (Settings && Settings->NetErrorPhase.IsValid())
		? Settings->NetErrorPhase
		: FlowTags::Phase_NetError;

	if (UFlow_GameFlowSubsystem* OwningFlow = Owner.Get())
	{
		// ForceTransition bypasses the allowed-transition + guard checks — this is error recovery.
		OwningFlow->ForceTransition(ErrorPhase);
	}
}

// ---------------------------------------------------------------------------------------------------
// Reads / helpers
// ---------------------------------------------------------------------------------------------------

FGameplayTag UFlow_MatchmakingController::GetMatchmakingPhase() const
{
	return PhaseToTag(LastObservedPhase);
}

void UFlow_MatchmakingController::GetLastResults(TArray<FSeam_SessionResult>& OutResults) const
{
	OutResults.Reset();
	if (UObject* Session = const_cast<UFlow_MatchmakingController*>(this)->ResolveSession())
	{
		ISeam_NetSession::Execute_GetSearchResults(Session, OutResults);
	}
}

FGameplayTag UFlow_MatchmakingController::PhaseToTag(ESeam_NetSessionPhase Phase)
{
	switch (Phase)
	{
	case ESeam_NetSessionPhase::Searching: return FlowTags::MMPhase_Searching;
	case ESeam_NetSessionPhase::Creating:  return FlowTags::MMPhase_Connecting;
	case ESeam_NetSessionPhase::Joining:   return FlowTags::MMPhase_Connecting;
	case ESeam_NetSessionPhase::InSession: return FlowTags::MMPhase_Active;
	case ESeam_NetSessionPhase::Failed:    return FlowTags::MMPhase_Failed;
	default:                               return FlowTags::MMPhase_Idle;
	}
}

void UFlow_MatchmakingController::BroadcastState(bool bTerminalFailure)
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}

	int32 ResultCount = 0;
	if (UObject* Session = ResolveSession())
	{
		TArray<FSeam_SessionResult> Results;
		ISeam_NetSession::Execute_GetSearchResults(Session, Results);
		ResultCount = Results.Num();
	}

	FFlow_MatchmakingPayload Payload;
	Payload.Phase = PhaseToTag(LastObservedPhase);
	Payload.RetryAttempt = RetryCount;
	Payload.bTerminalFailure = bTerminalFailure;
	Payload.ResultCount = ResultCount;

	Bus->BroadcastPayload(FlowTags::Bus_MatchmakingChanged, FInstancedStruct::Make(Payload), this);
}

UObject* UFlow_MatchmakingController::ResolveSession() const
{
	// Honour a live cached weak ref first; otherwise re-resolve through the locator.
	if (CachedSession.IsValid())
	{
		return CachedSession.GetObject();
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Obj = Locator->ResolveService(FlowTags::Service_NetSession);
	if (Obj && Obj->Implements<USeam_NetSession>())
	{
		if (ISeam_NetSession* AsInterface = Cast<ISeam_NetSession>(Obj))
		{
			const_cast<UFlow_MatchmakingController*>(this)->CachedSession = TWeakInterfacePtr<ISeam_NetSession>(*AsInterface);
		}
		return Obj;
	}
	return nullptr;
}

UObject* UFlow_MatchmakingController::ResolveLobby() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(FlowTags::Service_LobbyRead);
	return (Obj && Obj->Implements<USeam_LobbyRead>()) ? Obj : nullptr;
}

UDP_MessageBusSubsystem* UFlow_MatchmakingController::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}
