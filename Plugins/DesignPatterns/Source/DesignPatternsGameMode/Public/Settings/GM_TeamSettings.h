// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Team/GM_TeamTypes.h"
#include "GM_TeamSettings.generated.h"

/**
 * Project settings for the GameMode TEAMS / RESPAWN area. Every gameplay number here is a designer
 * tunable (no magic constants in code): team policy, friendly-fire mode and scalar, the roster of
 * assignable team tags used by auto-balance, the alliance table, and the respawn rules defaults.
 *
 * Appears under Project Settings -> Plugins -> "Design Patterns GameMode". The team subsystem reads the
 * CDO defensively: if it is somehow null it falls back to documented inert defaults (team-match policy,
 * friendly fire disabled) so the match still runs.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns GameMode"))
class DESIGNPATTERNSGAMEMODE_API UGM_TeamSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGM_TeamSettings();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	//~ End UDeveloperSettings

	/** How team relationships are interpreted (free-for-all / team / ally-faction). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team")
	EGM_TeamPolicy TeamPolicy = EGM_TeamPolicy::TeamMatch;

	/** How friendly fire is resolved by combat reading the team seam. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team")
	EGM_FriendlyFirePolicy FriendlyFirePolicy = EGM_FriendlyFirePolicy::Disabled;

	/**
	 * Damage scalar applied to friendly fire when FriendlyFirePolicy == Reduced (0..1). Ignored
	 * otherwise. Combat reads this through UGM_TeamSubsystem::GetFriendlyFireScalar.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team",
		meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "FriendlyFirePolicy == EGM_FriendlyFirePolicy::Reduced"))
	float FriendlyFireScalar = 0.25f;

	/**
	 * The roster of team tags auto-balance distributes joiners across (e.g. DP.Team.Red, DP.Team.Blue).
	 * Authored per game; empty disables auto-balance (actors keep whatever team they were set to).
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team", meta = (Categories = "DP.Team"))
	TArray<FGameplayTag> AssignableTeams;

	/** Explicit alliance pairs honored under AllyFaction policy (symmetric). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Team")
	TArray<FGM_TeamAllianceRow> Alliances;

	// --- Respawn defaults (a respawn component may override per-instance) ---

	/** Default delay, in seconds, between a respawn request and the actual respawn. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Respawn",
		meta = (ClampMin = "0.0", Units = "s"))
	float DefaultRespawnDelaySeconds = 5.0f;

	/**
	 * When true the respawn component prefers a spawn point whose filter tag matches the actor's team
	 * (team-aware spawning). When false any provided point is eligible.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	bool bTeamFilteredSpawning = true;

	/**
	 * Maximum number of automatic respawns per actor (<=0 means unlimited). Guards runaway respawn loops
	 * in modes with a life budget; the respawn component enforces it on authority.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	int32 MaxAutoRespawns = -1;

	/** Convenience accessor for the CDO. Never null in a running game. */
	static const UGM_TeamSettings* Get();
};
