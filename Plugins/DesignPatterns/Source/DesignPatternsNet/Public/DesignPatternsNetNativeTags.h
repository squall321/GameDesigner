// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsNet module's deep multiplayer layer.
 *
 * Only ROOT/anchor and service-locator/bus keys are declared here so the hierarchy is guaranteed to
 * exist at startup (tag-hierarchy matching on the message bus needs the parents to pre-exist).
 * Concrete leaf channels / damage channels / phase ids that are gameplay-specific are authored by the
 * GAME project as child tags — this module never bakes gameplay leaf tags.
 *
 * Service-locator keys (anchored under the core DP.Service root):
 *   DP.Service.Net.LagComp     — UNet_LagCompensationSubsystem registers itself (WeakObserved) so
 *                                Combat resolves it to register/validate hit-rewind targets.
 *   DP.Service.Net.Lobby       — ANet_LobbyState registers itself (WeakObserved) so UI/match-flow
 *                                resolve ISeam_LobbyRead by tag.
 *   DP.Service.Net.Scoreboard  — ANet_ScoreboardState registers itself so consumers resolve the
 *                                additive ISeam_ScoreSource provider.
 *
 * Message-bus channels (children of the core DP.Bus root, so a DP.Bus listener still receives them):
 *   DP.Bus.Net.Lobby.Changed      — the lobby roster / ready-state changed (cosmetic, state-derived).
 *   DP.Bus.Net.Lobby.AllReady     — every player is ready (match-start flow can begin countdown).
 *   DP.Bus.Net.HostMigration      — a host-migration was detected (UI should surface "reconnecting").
 *   DP.Bus.Net.HitConfirmed       — a lag-compensated hit was server-confirmed (cosmetic feedback).
 *   DP.Bus.Net.AntiCheat.Flagged  — a server-side validator rejected a client request (telemetry).
 *
 * Lobby phase anchor (children authored by the project):
 *   DP.Net.Lobby.Phase.*          — Filling / Countdown / Starting, returned by ISeam_LobbyRead::GetLobbyPhase.
 */
namespace NetNativeTags
{
	// ---- Service-locator keys (children of the core DP.Service root) ----

	/** Lag-compensation subsystem service key. */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Net_LagComp);

	/** Replicated lobby-state carrier service key (ISeam_LobbyRead). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Net_Lobby);

	/** Replicated scoreboard-state carrier service key (ISeam_ScoreSource). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Net_Scoreboard);

	// ---- Message-bus channel anchors (children of the core DP.Bus root) ----

	/** Root of every Net bus channel: DP.Bus.Net.* */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net);

	/** The lobby roster / ready / team state changed (state-derived notification, never a command). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net_Lobby_Changed);

	/** Every occupied lobby slot is ready and the minimum is met. */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net_Lobby_AllReady);

	/** A host-migration was detected (the lobby entered the migrating state). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net_HostMigration);

	/** A lag-compensated hit was confirmed on the server (cosmetic confirmation feedback). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net_HitConfirmed);

	/** A server-side request validator rejected a client request (anti-cheat telemetry). */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Net_AntiCheat_Flagged);

	// ---- Lobby phase anchor (project authors the leaf phases) ----

	/** Root of the lobby phase enum-as-tags: DP.Net.Lobby.Phase.* */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Net_Lobby_Phase);

	/** The lobby is filling and accepting players. */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Net_Lobby_Phase_Filling);

	/** All players ready; a start countdown is running. */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Net_Lobby_Phase_Countdown);

	/** The countdown finished; the server is travelling to the match map. */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Net_Lobby_Phase_Starting);

	/**
	 * Root of the lobby party anchor: DP.Net.Lobby.Party.*. The party component derives a deterministic
	 * default party tag under this when a project supplies no Party.* tag table of its own.
	 */
	DESIGNPATTERNSNET_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Net_Lobby_Party);
}
