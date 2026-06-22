// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_NetSession.generated.h"

/** Where a net session currently is. Derived purely from the online subsystem's real bool delegates. */
UENUM(BlueprintType)
enum class ESeam_NetSessionPhase : uint8
{
	Idle,
	Searching,
	Creating,
	Joining,
	InSession,
	Failed
};

/** Parameters for a session search. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_SessionQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	int32 MaxResults = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	bool bLAN = false;
};

/** Parameters for creating a session. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_SessionDesc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	int32 MaxPlayers = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	bool bLAN = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	bool bUsePresence = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Seam|Net")
	FString MapName;
};

/** One found session, flat and net/UI-safe. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_SessionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net")
	int32 ResultIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net")
	FString OwningName;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net")
	int32 OpenSlots = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net")
	int32 PingMs = 0;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_NetSession : public UInterface
{
	GENERATED_BODY()
};

/**
 * Net-session seam, owned by the Net module's session subsystem. The GameFlow app-flow FSM drives
 * matchmaking (search/create/join) through it without depending on the Net module. Results are flat
 * value structs; the phase is derived from the online subsystem's real completion delegates.
 */
class DESIGNPATTERNSSEAMS_API ISeam_NetSession
{
	GENERATED_BODY()

public:
	/** Current session phase. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	ESeam_NetSessionPhase GetSessionPhase() const;

	/** Begin an async search; phase moves to Searching. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	void FindSessions(const FSeam_SessionQuery& Query);

	/** The most recent search results (valid once phase returned from Searching). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	void GetSearchResults(TArray<FSeam_SessionResult>& OutResults) const;

	/** Begin creating/hosting a session; phase moves to Creating. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	void CreateSession(const FSeam_SessionDesc& Desc);

	/** Begin joining a found session by result index; phase moves to Joining. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	void JoinSession(int32 ResultIndex);

	/** Leave / destroy the current session; phase returns to Idle. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net")
	void LeaveSession();
};
