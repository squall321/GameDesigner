// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsGameMode module.
 *
 * These tags anchor the module's hierarchy at startup so tag-hierarchy matching (message-bus
 * channels, service-locator keys, match-state tags) always works even before the project's tag
 * config loads. Full tag strings (and their human-readable comments) live in
 * DesignPatternsGameModeModule.cpp via UE_DEFINE_GAMEPLAY_TAG_COMMENT.
 *
 * Conventions:
 *  - DP.GM.Match.*    : match-state FSM tags consumed by UGM_MatchStateComponent.
 *  - DP.Score.*       : tag-keyed scoreboard buckets the score subsystem/carrier use as defaults.
 *  - DP.Service.GM.*  : service-locator keys under which GameMode providers register (the score
 *                       seam, the team seam, etc.) so other modules resolve them WITHOUT a hard
 *                       dependency on this module.
 *  - DP.Bus.GM.*      : message-bus channels broadcast when match state / scores change, so HUD,
 *                       audio, analytics and game-flow react without coupling to this module.
 *
 * Concrete team/score bucket tags beyond the documented defaults are authored by the game project
 * (in its tag config or its own native tags); this header only anchors the roots the plumbing relies on.
 */
namespace GameModeNativeTags
{
	// --- Module root ---------------------------------------------------------------------------

	/** Root anchor for everything this module defines (DP.GM). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GM);

	// --- Match-state FSM tags (DP.GM.Match.*) --------------------------------------------------

	/** Parent of the match-state tags; never set as an active state itself. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match);

	/** Pre-match lobby/warmup: waiting for the start condition (players ready, timer, etc.). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_WaitingToStart);

	/** Active play: ruleset win/lose conditions are evaluated each authority tick. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_InProgress);

	/** A round has ended but the match continues (intermission before the next round). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_RoundOver);

	/** The match is fully decided; scores are final and a results screen is safe to show. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Match_MatchOver);

	// --- Default scoreboard buckets (DP.Score.*) -----------------------------------------------

	/** Root anchor for tag-keyed scoreboard buckets. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Score);

	/** Default neutral bucket used when no team/category key is supplied by the caller. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Score_Default);

	// --- Service-locator keys (DP.Service.GM.*) ------------------------------------------------

	/** Service key under which the score carrier registers its ISeam_ScoreSource read seam. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_GM_Score);

	/**
	 * Service key under which UGM_TeamSubsystem publishes its ISeam_TeamAffinity provider. Combat, AI,
	 * HUD and respawn resolve a TScriptInterface<ISeam_TeamAffinity> from this key without a hard
	 * dependency on this module. Registered WeakObserved (the provider is world-lifetime).
	 */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_GM_TeamAffinity);

	// --- Message-bus channels (DP.Bus.GM.*) ----------------------------------------------------

	/** Broadcast when the match state changes; payload carries the old and new state tags. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_MatchStateChanged);

	/** Broadcast when a score bucket changes; payload carries the key, new value, and delta. */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_ScoreChanged);

	/** Broadcast when the match is decided; payload carries the winning key (empty for a draw). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_MatchDecided);

	/** Broadcast (authority) when an actor's team assignment changes (payload FGM_TeamChangedPayload). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_TeamChanged);

	/** Broadcast (authority) when a respawn is requested and queued (payload FGM_RespawnedPayload). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_RespawnRequested);

	/** Broadcast (authority) when an actor has been respawned (payload FGM_RespawnedPayload). */
	DESIGNPATTERNSGAMEMODE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GM_Respawned);
}
