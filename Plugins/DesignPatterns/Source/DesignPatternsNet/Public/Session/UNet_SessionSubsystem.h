// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Session/FNet_SessionSettings.h"

// When the online stack is available we need its session-interface types (e.g.
// EOnJoinSessionCompleteResult) for the native handler signatures below. This include is fully
// guarded so the header has ZERO online dependency in offline-only builds. NOTE: the .generated.h
// include below MUST remain the LAST include — so this guarded include sits above it.
#if WITH_DP_ONLINE
#include "Interfaces/OnlineSessionInterface.h"
#endif

#include "UNet_SessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnCreateSessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNet_OnFindSessionsComplete, bool, bWasSuccessful, const TArray<FNet_SessionSearchResult>&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnJoinSessionComplete, bool, bWasSuccessful);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNet_OnDestroySessionComplete, bool, bWasSuccessful);

/**
 * Thin, Blueprint-friendly wrapper over the Online Subsystem's IOnlineSession for the common
 * Create / Find / Join / Destroy session lifecycle.
 *
 * GameInstance-scoped (sessions outlive level travel), deriving from the core's
 * UDP_GameInstanceSubsystem so it participates in the plugin's standardized lifecycle, debug
 * string, and verbose-logging conventions (HARD RULE 5).
 *
 * AVAILABILITY: the whole online integration is compile-guarded behind WITH_DP_ONLINE (set by the
 * Build.cs when the OnlineSubsystem modules are present). When the online stack is absent, every
 * entry point logs "online unavailable" and immediately fires its completion delegate with
 * bWasSuccessful = false, so callers and UI behave deterministically in either configuration.
 *
 * Resolution: the active online subsystem is fetched via Online::GetSubsystem(GetWorld()).
 */
UCLASS()
class DESIGNPATTERNSNET_API UNet_SessionSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	// ---- Session lifecycle (Blueprint-callable) ----

	/**
	 * Create and host a session described by Settings. Fires OnCreateSessionComplete when done.
	 * No-op + immediate failure delegate when the online stack is unavailable.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Session")
	void CreateSession(const FNet_SessionSettings& Settings);

	/**
	 * Search for joinable sessions. MaxSearchResults caps the result count; bLANQuery selects a LAN
	 * vs. internet search. Fires OnFindSessionsComplete with the results.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Session")
	void FindSessions(int32 MaxSearchResults = 20, bool bLANQuery = false);

	/**
	 * Join a previously-found session by its result index (from a FNet_SessionSearchResult). Fires
	 * OnJoinSessionComplete; on success the caller should ClientTravel to the resolved connect URL,
	 * which is logged and available via GetPendingConnectURL().
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Session")
	void JoinSession(int32 SearchResultIndex);

	/** Destroy the current session (leave/host shutdown). Fires OnDestroySessionComplete. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Session")
	void DestroySession();

	/** True if the online subsystem is present in this build/runtime. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Session")
	bool IsOnlineAvailable() const;

	/** The connect URL resolved by the last successful JoinSession (empty if none / unavailable). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Session")
	FString GetPendingConnectURL() const { return PendingConnectURL; }

	// ---- Delegates ----

	/** Broadcast when CreateSession finishes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Session")
	FNet_OnCreateSessionComplete OnCreateSessionComplete;

	/** Broadcast when FindSessions finishes (with the result list). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Session")
	FNet_OnFindSessionsComplete OnFindSessionsComplete;

	/** Broadcast when JoinSession finishes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Session")
	FNet_OnJoinSessionComplete OnJoinSessionComplete;

	/** Broadcast when DestroySession finishes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Session")
	FNet_OnDestroySessionComplete OnDestroySessionComplete;

private:
	/** The session name we host/join under (engine convention: NAME_GameSession). */
	FName SessionName;

	/** Resolved connect URL from the last successful join. */
	FString PendingConnectURL;

	/** Cached copy of the settings used for the in-flight CreateSession (for travel/map info). */
	FNet_SessionSettings PendingCreateSettings;

#if WITH_DP_ONLINE
	// Native online integration. Declared only when the online stack is available so the header
	// stays dependency-free in offline-only builds. Implementations live in the .cpp behind the same guard.

	/** Bind the IOnlineSession completion delegates. Called from Initialize. */
	void BindOnlineDelegates();

	/** Unbind on Deinitialize. */
	void ClearOnlineDelegates();

	// Native completion handlers (forwarded to the dynamic delegates above).
	void HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful);

	// Native delegate handles, stored so we can cleanly unbind.
	FDelegateHandle CreateCompleteHandle;
	FDelegateHandle FindCompleteHandle;
	FDelegateHandle JoinCompleteHandle;
	FDelegateHandle DestroyCompleteHandle;

	/** The shared search settings kept alive across the async FindSessions call. */
	TSharedPtr<class FOnlineSessionSearch> SearchSettings;
#endif // WITH_DP_ONLINE
};
