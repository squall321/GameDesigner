// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FNet_SessionSettings.generated.h"

/**
 * Designer-friendly description of an online session to create or search for.
 *
 * Deliberately a plain value type with no Online Subsystem dependency in its header, so gameplay
 * and UI code can pass it around even in builds where OnlineSubsystem is absent. UNet_SessionSubsystem
 * translates it into the engine's FOnlineSessionSettings when (and only when) the online stack exists.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_SessionSettings
{
	GENERATED_BODY()

	/** Maximum number of players (public connections) allowed in the session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session", meta = (ClampMin = "1"))
	int32 MaxPlayers = 4;

	/** True for a LAN session (no online service / matchmaking); false for an internet session. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session")
	bool bIsLAN = false;

	/**
	 * True to advertise presence (friends can see/join). Presence sessions route through the
	 * platform's social layer; LAN sessions ignore this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session")
	bool bUsesPresence = true;

	/** True to allow players to join while the match is in progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session")
	bool bAllowJoinInProgress = true;

	/** Map (level) name the session will travel to / is hosting. Stored as a session setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session")
	FString MapName = TEXT("/Game/Maps/Lobby");

	/** Free-form match type tag advertised on the session (e.g. "Coop", "Deathmatch"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Session")
	FString MatchType = TEXT("Default");
};

/**
 * Lightweight, Blueprint-exposable handle to a discovered session search result. Wraps just the
 * fields a UI needs to display and select a result; the subsystem keeps the heavyweight native
 * search result internally and resolves it back by index on Join.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_SessionSearchResult
{
	GENERATED_BODY()

	/** Index into the subsystem's last search results array. Pass back to JoinSession. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	int32 ResultIndex = INDEX_NONE;

	/** Display name of the host / session owner. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	FString OwningPlayerName;

	/** Advertised current player count. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	int32 CurrentPlayers = 0;

	/** Advertised maximum player count. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	int32 MaxPlayers = 0;

	/** Measured ping to the host in milliseconds (-1 if unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	int32 PingMs = -1;

	/** Advertised match type, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Session")
	FString MatchType;
};
