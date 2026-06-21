// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) tags for the DesignPatternsMovement module.
 *
 * Two families:
 *   - Move.State.*    — identity tags for the movement state machine's states. A movement
 *                       UDP_StateMachineDefinition authors UDP_State objects whose StateTag is one of
 *                       these, and transition guards reference them.
 *   - Move.Need.*     — the need tag the UMove_StaminaComponent answers through ISeam_NeedProvider.
 *   - Move.Request.*  — special-move request tags consumed via ISeam_MovementController and stamped
 *                       on the blackboard as one-shot tokens by the intent component.
 *   - Move.Status.*   — loose owned tags the dash action / states add to the owner's
 *                       UDP_GameplayActionComponent (e.g. the shared i-frame tag enforced by Combat).
 *   - DP.Bus.Move.*   — cosmetic feedback channels under the core DP.Bus root (hierarchy matching).
 *   - DP.Service.Move.*— service-locator keys for movement providers.
 *   - Move.BB.*        — reserved namespace documenting blackboard key names (FName-keyed at runtime).
 *
 * The full tag strings are defined in Move_NativeTags.cpp.
 *
 * NOTE the shared i-frame tag: per the architecture plan, dash i-frames and combat dodge i-frames both
 * ride the SAME loose owned tag "DP.Combat.Status.IFrame" on the existing UDP_GameplayActionComponent,
 * enforced by Combat's UCombat_IFrameAwareDamageExecution. Movement does NOT define a parallel invuln
 * system; it only adds/removes that tag. The tag string is mirrored here as a native tag so the dash
 * action can reference it without including any Combat header. (Native gameplay tags are de-duplicated
 * by string across modules, so declaring the same string in two modules is safe.)
 */
namespace MoveNativeTags
{
	// ---- State identity tags (Move.State.*) ----
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Walk);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Run);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Sprint);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Crouch);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Slide);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Jump);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_DoubleJump);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dash);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_WallRun);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Climb);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Mantle);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Vault);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Swim);

	// ---- Need tag (Move.Need.*) ----
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need_Stamina);

	// ---- Special-move request tags (Move.Request.*) ----
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Dash);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Dodge);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Mantle);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Vault);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Jump);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_WallRun);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Request_Climb);

	// ---- Loose owned status tags (Move.Status.* and the shared Combat i-frame tag) ----
	// The shared i-frame tag string — identical to Combat's, intentionally. Adding/removing it on the
	// UDP_GameplayActionComponent is the single cross-module invulnerability contract.
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_IFrame);
	// Blocks new special-move requests while a traversal is in progress.
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Status_Traversing);

	// ---- Bus channels (DP.Bus.Move.*) ----
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Move_StateChanged);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Move_SpecialMove);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Move_Landed);
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Move_StaminaDepleted);

	// ---- Service-locator keys (DP.Service.Move.*) ----
	DESIGNPATTERNSMOVEMENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Move_Stamina);
}
