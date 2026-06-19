// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Session/UNet_SessionSubsystem.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"

#if WITH_DP_ONLINE
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "Interfaces/OnlineSessionInterface.h"
#endif

namespace
{
	// Engine-conventional advertised session setting keys.
	static const FName Net_SettingKey_MapName(TEXT("DP_MAPNAME"));
	static const FName Net_SettingKey_MatchType(TEXT("DP_MATCHTYPE"));
}

void UNet_SessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Engine convention for the primary session name.
	SessionName = NAME_GameSession;

#if WITH_DP_ONLINE
	BindOnlineDelegates();
	UE_LOG(LogDP, Log, TEXT("UNet_SessionSubsystem initialized (OnlineSubsystem present)."));
#else
	UE_LOG(LogDP, Log, TEXT("UNet_SessionSubsystem initialized (no OnlineSubsystem; offline-only fallback)."));
#endif
}

void UNet_SessionSubsystem::Deinitialize()
{
#if WITH_DP_ONLINE
	ClearOnlineDelegates();
#endif
	Super::Deinitialize();
}

FString UNet_SessionSubsystem::GetDPDebugString_Implementation() const
{
#if WITH_DP_ONLINE
	const TCHAR* Avail = TEXT("online:available");
#else
	const TCHAR* Avail = TEXT("online:unavailable");
#endif
	return FString::Printf(TEXT("UNet_SessionSubsystem [%s] session='%s' connectURL='%s'"),
		Avail, *SessionName.ToString(),
		PendingConnectURL.IsEmpty() ? TEXT("<none>") : *PendingConnectURL);
}

bool UNet_SessionSubsystem::IsOnlineAvailable() const
{
#if WITH_DP_ONLINE
	return Online::GetSubsystem(GetWorld()) != nullptr;
#else
	return false;
#endif
}

#if !WITH_DP_ONLINE

// ----------------------------------------------------------------------------------------------
// Offline-only fallback — compiled when the project ships without an OnlineSubsystem (e.g. a
// single-player title, or a platform where online is disabled). This is complete, intentional
// behaviour: each entry point reports that online sessions are unsupported in this configuration
// and fires its completion delegate with a deterministic failure result, so calling code follows
// exactly one well-defined path regardless of build configuration. To enable real sessions, add
// OnlineSubsystem + OnlineSubsystemUtils to the project (see this module's Build.cs, which
// auto-detects them and defines WITH_DP_ONLINE).
// ----------------------------------------------------------------------------------------------

void UNet_SessionSubsystem::CreateSession(const FNet_SessionSettings& /*Settings*/)
{
	UE_LOG(LogDP, Warning, TEXT("CreateSession: this build has no OnlineSubsystem; online sessions are unsupported."));
	OnCreateSessionComplete.Broadcast(false);
}

void UNet_SessionSubsystem::FindSessions(int32 /*MaxSearchResults*/, bool /*bLANQuery*/)
{
	UE_LOG(LogDP, Warning, TEXT("FindSessions: this build has no OnlineSubsystem; online sessions are unsupported."));
	OnFindSessionsComplete.Broadcast(false, TArray<FNet_SessionSearchResult>());
}

void UNet_SessionSubsystem::JoinSession(int32 /*SearchResultIndex*/)
{
	UE_LOG(LogDP, Warning, TEXT("JoinSession: this build has no OnlineSubsystem; online sessions are unsupported."));
	OnJoinSessionComplete.Broadcast(false);
}

void UNet_SessionSubsystem::DestroySession()
{
	UE_LOG(LogDP, Warning, TEXT("DestroySession: this build has no OnlineSubsystem; online sessions are unsupported."));
	OnDestroySessionComplete.Broadcast(false);
}

#else // WITH_DP_ONLINE

// ----------------------------------------------------------------------------------------------
// REAL IMPLEMENTATIONS — built against IOnlineSession.
// ----------------------------------------------------------------------------------------------

void UNet_SessionSubsystem::BindOnlineDelegates()
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	if (!OSS)
	{
		UE_LOG(LogDP, Warning, TEXT("UNet_SessionSubsystem: no online subsystem at bind time."));
		return;
	}
	const IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("UNet_SessionSubsystem: online subsystem has no session interface."));
		return;
	}

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UNet_SessionSubsystem::HandleCreateSessionComplete));
	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UNet_SessionSubsystem::HandleFindSessionsComplete));
	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UNet_SessionSubsystem::HandleJoinSessionComplete));
	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UNet_SessionSubsystem::HandleDestroySessionComplete));
}

void UNet_SessionSubsystem::ClearOnlineDelegates()
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	if (!OSS)
	{
		return;
	}
	const IOnlineSessionPtr Sessions = OSS->GetSessionInterface();
	if (!Sessions.IsValid())
	{
		return;
	}

	Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
}

void UNet_SessionSubsystem::CreateSession(const FNet_SessionSettings& Settings)
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("CreateSession: no session interface."));
		OnCreateSessionComplete.Broadcast(false);
		return;
	}

	// If a session already exists under this name, tear it down first.
	if (Sessions->GetNamedSession(SessionName) != nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("CreateSession: destroying stale session before recreating."));
		Sessions->DestroySession(SessionName);
	}

	PendingCreateSettings = Settings;

	TSharedRef<FOnlineSessionSettings> OnlineSettings = MakeShared<FOnlineSessionSettings>();
	OnlineSettings->NumPublicConnections   = FMath::Max(1, Settings.MaxPlayers);
	OnlineSettings->NumPrivateConnections  = 0;
	OnlineSettings->bIsLANMatch            = Settings.bIsLAN;
	OnlineSettings->bUsesPresence          = Settings.bUsesPresence;
	OnlineSettings->bAllowJoinViaPresence  = Settings.bUsesPresence;
	OnlineSettings->bShouldAdvertise       = true;
	OnlineSettings->bAllowJoinInProgress   = Settings.bAllowJoinInProgress;
	OnlineSettings->bUseLobbiesIfAvailable = Settings.bUsesPresence;
	OnlineSettings->bAllowInvites          = Settings.bUsesPresence;

	OnlineSettings->Set(Net_SettingKey_MapName, Settings.MapName, EOnlineDataAdvertisementType::ViaOnlineService);
	OnlineSettings->Set(Net_SettingKey_MatchType, Settings.MatchType, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	const int32 ControllerId = 0; // Primary local player; games with multiple locals can extend this.
	if (!Sessions->CreateSession(ControllerId, SessionName, *OnlineSettings))
	{
		UE_LOG(LogDP, Warning, TEXT("CreateSession: CreateSession() returned false (sync failure)."));
		OnCreateSessionComplete.Broadcast(false);
	}
}

void UNet_SessionSubsystem::FindSessions(int32 MaxSearchResults, bool bLANQuery)
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("FindSessions: no session interface."));
		OnFindSessionsComplete.Broadcast(false, TArray<FNet_SessionSearchResult>());
		return;
	}

	SearchSettings = MakeShared<FOnlineSessionSearch>();
	SearchSettings->MaxSearchResults = FMath::Max(1, MaxSearchResults);
	SearchSettings->bIsLanQuery = bLANQuery;
	// Only surface presence-advertised sessions when doing an internet search.
	if (!bLANQuery)
	{
		SearchSettings->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);
	}

	const int32 ControllerId = 0;
	if (!Sessions->FindSessions(ControllerId, SearchSettings.ToSharedRef()))
	{
		UE_LOG(LogDP, Warning, TEXT("FindSessions: FindSessions() returned false (sync failure)."));
		OnFindSessionsComplete.Broadcast(false, TArray<FNet_SessionSearchResult>());
	}
}

void UNet_SessionSubsystem::JoinSession(int32 SearchResultIndex)
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("JoinSession: no session interface."));
		OnJoinSessionComplete.Broadcast(false);
		return;
	}
	if (!SearchSettings.IsValid() || !SearchSettings->SearchResults.IsValidIndex(SearchResultIndex))
	{
		UE_LOG(LogDP, Warning, TEXT("JoinSession: invalid result index %d."), SearchResultIndex);
		OnJoinSessionComplete.Broadcast(false);
		return;
	}

	const int32 ControllerId = 0;
	if (!Sessions->JoinSession(ControllerId, SessionName, SearchSettings->SearchResults[SearchResultIndex]))
	{
		UE_LOG(LogDP, Warning, TEXT("JoinSession: JoinSession() returned false (sync failure)."));
		OnJoinSessionComplete.Broadcast(false);
	}
}

void UNet_SessionSubsystem::DestroySession()
{
	IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
	const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("DestroySession: no session interface."));
		OnDestroySessionComplete.Broadcast(false);
		return;
	}
	if (!Sessions->DestroySession(SessionName))
	{
		UE_LOG(LogDP, Warning, TEXT("DestroySession: DestroySession() returned false (sync failure)."));
		OnDestroySessionComplete.Broadcast(false);
	}
}

void UNet_SessionSubsystem::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	UE_LOG(LogDP, Log, TEXT("CreateSession complete (session='%s', success=%d)."),
		*InSessionName.ToString(), bWasSuccessful ? 1 : 0);
	OnCreateSessionComplete.Broadcast(bWasSuccessful);
}

void UNet_SessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	TArray<FNet_SessionSearchResult> Results;
	if (bWasSuccessful && SearchSettings.IsValid())
	{
		Results.Reserve(SearchSettings->SearchResults.Num());
		for (int32 Index = 0; Index < SearchSettings->SearchResults.Num(); ++Index)
		{
			const FOnlineSessionSearchResult& Native = SearchSettings->SearchResults[Index];

			FNet_SessionSearchResult Out;
			Out.ResultIndex      = Index;
			Out.OwningPlayerName = Native.Session.OwningUserName;
			Out.MaxPlayers       = Native.Session.SessionSettings.NumPublicConnections;
			Out.CurrentPlayers   = Native.Session.SessionSettings.NumPublicConnections
								   - Native.Session.NumOpenPublicConnections;
			Out.PingMs           = Native.PingInMs;
			Native.Session.SessionSettings.Get(Net_SettingKey_MatchType, Out.MatchType);

			Results.Add(Out);
		}
	}

	UE_LOG(LogDP, Log, TEXT("FindSessions complete (success=%d, results=%d)."),
		bWasSuccessful ? 1 : 0, Results.Num());
	OnFindSessionsComplete.Broadcast(bWasSuccessful, Results);
}

void UNet_SessionSubsystem::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const bool bSuccess = (Result == EOnJoinSessionCompleteResult::Success);
	PendingConnectURL.Reset();

	if (bSuccess)
	{
		IOnlineSubsystem* OSS = Online::GetSubsystem(GetWorld());
		const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
		if (Sessions.IsValid())
		{
			// Resolve the connect string callers ClientTravel to.
			Sessions->GetResolvedConnectString(InSessionName, PendingConnectURL);
		}
	}

	UE_LOG(LogDP, Log, TEXT("JoinSession complete (success=%d, url='%s')."),
		bSuccess ? 1 : 0, PendingConnectURL.IsEmpty() ? TEXT("<none>") : *PendingConnectURL);
	OnJoinSessionComplete.Broadcast(bSuccess);
}

void UNet_SessionSubsystem::HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	PendingConnectURL.Reset();
	UE_LOG(LogDP, Log, TEXT("DestroySession complete (session='%s', success=%d)."),
		*InSessionName.ToString(), bWasSuccessful ? 1 : 0);
	OnDestroySessionComplete.Broadcast(bWasSuccessful);
}

#endif // WITH_DP_ONLINE
