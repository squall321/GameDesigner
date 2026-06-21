// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Combat_DeepNativeTags.h"

namespace CombatDeepNativeTags
{
	// ---- Damage channels ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Generic,   "DP.Combat.Damage.Generic",   "Generic/untyped damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Physical,  "DP.Combat.Damage.Physical",  "Physical/impact damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Fire,      "DP.Combat.Damage.Fire",      "Fire/burn damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Frost,     "DP.Combat.Damage.Frost",     "Frost/cold damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Lightning, "DP.Combat.Damage.Lightning", "Lightning/shock damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Poison,    "DP.Combat.Damage.Poison",    "Poison damage channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_True,      "DP.Combat.Damage.True",      "True/unmitigated damage channel.");

	// ---- Hit-reaction classifications ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Hit,       "DP.Combat.Reaction.Hit",       "A normal confirmed hit.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Blocked,   "DP.Combat.Reaction.Blocked",   "Hit blocked by an active guard.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Parried,   "DP.Combat.Reaction.Parried",   "Hit was parried.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Dodged,    "DP.Combat.Reaction.Dodged",    "Hit fully avoided (dodge/i-frame).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Weakpoint, "DP.Combat.Reaction.Weakpoint", "Hit landed on a weakpoint hitzone.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Critical,  "DP.Combat.Reaction.Critical",  "Hit was a critical strike.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reaction_Stagger,   "DP.Combat.Reaction.Stagger",   "Hit broke poise and staggered the victim.");

	// ---- Status-effect family roots ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Family_Bleed,  "DP.Combat.Status.Family.Bleed",  "Bleed family root.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Family_Poison, "DP.Combat.Status.Family.Poison", "Poison family root.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Family_Burn,   "DP.Combat.Status.Family.Burn",   "Burn family root.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Family_Stun,   "DP.Combat.Status.Family.Stun",   "Stun/CC family root.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Family_Slow,   "DP.Combat.Status.Family.Slow",   "Slow/chill family root.");

	// ---- Transient combat status owned-tags ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_IFrame,     "DP.Combat.Status.IFrame",     "Invulnerability window (shared dodge/dash i-frame tag).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Blocking,   "DP.Combat.Status.Blocking",   "Active block/guard stance.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Parrying,   "DP.Combat.Status.Parrying",   "Active parry window.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Hyperarmor, "DP.Combat.Status.Hyperarmor", "Poise damage absorbed without staggering.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Staggered,  "DP.Combat.Status.Staggered",  "Victim's poise broke; in hitstun.");

	// ---- Need tags ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Need_Guard, "DP.Combat.Need.Guard", "Normalized guard-meter fullness need.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Need_Poise, "DP.Combat.Need.Poise", "Normalized poise fullness need.");

	// ---- Stat-modifier source keys ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(StatSource_Status, "DP.Combat.StatSource.Status", "Source key for status-derived stat modifiers.");

	// ---- Message-bus channels ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_HitFeedback, "DP.Bus.Combat.HitFeedback", "Local cosmetic hit-feedback channel.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_Staggered,   "DP.Bus.Combat.Staggered",   "Local stagger-feedback channel.");
}
