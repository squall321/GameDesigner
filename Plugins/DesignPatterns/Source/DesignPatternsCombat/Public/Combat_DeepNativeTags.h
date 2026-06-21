// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) tags for the DEEP combat layer (pipeline, defense, poise, weakpoints,
 * status families, hit-reactions and i-frames).
 *
 * These extend the shipped CombatNativeTags set additively — the original Combat_NativeTags.h is
 * untouched. They are split into a separate header/namespace so the deep layer pulls only what it
 * needs and the original surface stays stable.
 *
 * Anchoring conventions (mirrors the core's hierarchy so tag-prefix matching works):
 *  - DP.Combat.Damage.*          : damage channels (the seam-neutral damage type used by resistances).
 *  - DP.Combat.Reaction.*        : hit-reaction classifications fed to ISeam_DamageReactor::OnDamageResolved.
 *  - DP.Combat.Status.Family.*   : status-effect family roots (poison/bleed/burn...) for cross-effect DR.
 *  - DP.Combat.Status.*          : transient combat status owned-tags (i-frames, hyperarmor, guard, stagger).
 *  - DP.Combat.Need.*            : need tags exposed by the defense component via ISeam_NeedProvider.
 *  - DP.Combat.StatSource.*      : SourceTag keys for ISeam_StatModifierSink derived-modifier groups.
 *  - DP.Bus.Combat.*             : message-bus channels (UNDER the core DP.Bus root) for decoupled feedback.
 *
 * The full tag strings are defined in Combat_DeepNativeTags.cpp.
 */
namespace CombatDeepNativeTags
{
	// ---- Damage channels (map onto ECombat_DamageType for resistance lookups / seam payloads) ----

	/** Generic / untyped damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Generic);
	/** Physical / impact damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Physical);
	/** Fire / burn damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Fire);
	/** Frost / cold damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Frost);
	/** Lightning / shock damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Lightning);
	/** Poison damage channel. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_Poison);
	/** True / unmitigated damage channel (bypasses resistances). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Damage_True);

	// ---- Hit-reaction classifications (the ReactionTag passed to ISeam_DamageReactor) ----

	/** A normal (non-special) confirmed hit. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Hit);
	/** The hit was blocked by an active guard (reduced / chip damage). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Blocked);
	/** The hit was parried (attacker is the one who should stagger). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Parried);
	/** The hit was fully avoided (dodge / i-frame). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Dodged);
	/** The hit landed on a weakpoint hitzone. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Weakpoint);
	/** The hit was a critical strike. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Critical);
	/** The hit broke the victim's poise and staggered them. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reaction_Stagger);

	// ---- Status-effect family roots (cross-effect diminishing-returns / immunity keys) ----

	/** Bleed family root. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Family_Bleed);
	/** Poison family root. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Family_Poison);
	/** Burn family root. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Family_Burn);
	/** Stun / crowd-control family root. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Family_Stun);
	/** Slow / chill family root. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Family_Slow);

	// ---- Transient combat status owned-tags (added to UDP_GameplayActionComponent's owned tags) ----

	/**
	 * Invulnerability window. The SINGLE i-frame owner-tag the whole plugin agrees on (Combat dodge AND
	 * Movement dash both add it). UCombat_IFrameAwareDamageExecution returns 0 when the victim carries it.
	 */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_IFrame);
	/** Active block / guard stance (set while a block is held). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Blocking);
	/** Active parry window (a short timed window around a block start). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Parrying);
	/** Hyperarmor: poise damage is absorbed without staggering while present. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Hyperarmor);
	/** Staggered: the victim's poise broke and they are in hitstun. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Staggered);

	// ---- Need tags (defense component answers these via ISeam_NeedProvider) ----

	/** Normalized guard-meter fullness, exposed as a need so brains/UI read it generically. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need_Guard);
	/** Normalized poise fullness, exposed as a need. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need_Poise);

	// ---- Stat-modifier source keys (SourceTag passed to ISeam_StatModifierSink::SetDerivedModifierGroup) ----

	/** SourceTag root under which status-effect-derived stat modifiers are contributed. */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(StatSource_Status);

	// ---- Message-bus channels (under the core DP.Bus root) ----

	/** Broadcast (locally) when a hit's cosmetic feedback should play (hitstop / VFX / UI). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_HitFeedback);
	/** Broadcast (locally) when an actor is staggered (poise broken). */
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_Staggered);
}
