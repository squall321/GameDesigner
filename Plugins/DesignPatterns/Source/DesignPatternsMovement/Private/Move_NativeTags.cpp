// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Move_NativeTags.h"

namespace MoveNativeTags
{
	// ---- State identity tags ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Walk, "Move.State.Walk",
		"Grounded locomotion at the base walk speed (default ground state).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Run, "Move.State.Run",
		"Grounded locomotion at the run speed (full analog stick, no sprint modifier).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Sprint, "Move.State.Sprint",
		"Grounded locomotion at sprint speed; drains stamina on authority.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Crouch, "Move.State.Crouch",
		"Crouched grounded locomotion at reduced speed and capsule height.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Slide, "Move.State.Slide",
		"Momentum slide entered from a sprinting crouch; slope-aware acceleration.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Jump, "Move.State.Jump",
		"First airborne jump arc.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_DoubleJump, "Move.State.DoubleJump",
		"Additional mid-air jump consuming an air-jump budget tracked on the blackboard.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dash, "Move.State.Dash",
		"Brief high-velocity directional burst; pairs with the i-frame dash action.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_WallRun, "Move.State.WallRun",
		"Lateral run along a near-vertical surface while a wall is detected.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Climb, "Move.State.Climb",
		"Free climb on a climbable surface (flying movement mode, gravity off).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Mantle, "Move.State.Mantle",
		"Pull-up onto a ledge; interpolates to a server-validated target transform.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Vault, "Move.State.Vault",
		"Vault over a low, shallow obstacle; interpolates to a server-validated target transform.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Swim, "Move.State.Swim",
		"Swimming inside a water volume (CMC swimming movement mode).");

	// ---- Need tag ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Need_Stamina, "Move.Need.Stamina",
		"The stamina meter answered by UMove_StaminaComponent through ISeam_NeedProvider.");

	// ---- Special-move request tags ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Dash, "Move.Request.Dash",
		"Pending dash request, consumed via ISeam_MovementController::ConsumeSpecialMoveRequest.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Dodge, "Move.Request.Dodge",
		"Pending dodge request (a dash variant that always grants i-frames).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Mantle, "Move.Request.Mantle",
		"Pending mantle request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Vault, "Move.Request.Vault",
		"Pending vault request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Jump, "Move.Request.Jump",
		"Pending jump / double-jump request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_WallRun, "Move.Request.WallRun",
		"Pending wall-run attach request.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Request_Climb, "Move.Request.Climb",
		"Pending climb attach request.");

	// ---- Loose owned status tags ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_IFrame, "DP.Combat.Status.IFrame",
		"Shared invulnerability-frame tag; Combat's i-frame damage execution returns 0 while present. "
		"Movement's dash action adds/removes it. Identical string to Combat's native i-frame tag.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Status_Traversing, "Move.Status.Traversing",
		"Present while a mantle/vault/traversal is interpolating; blocks new special-move requests.");

	// ---- Bus channels ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Move_StateChanged, "DP.Bus.Move.StateChanged",
		"Broadcast (locally) when the movement state machine changes state.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Move_SpecialMove, "DP.Bus.Move.SpecialMove",
		"Broadcast (locally) when a special move (dash/mantle/vault) is committed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Move_Landed, "DP.Bus.Move.Landed",
		"Broadcast (locally) when the character lands from an airborne state.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Move_StaminaDepleted, "DP.Bus.Move.StaminaDepleted",
		"Broadcast (locally) when stamina reaches zero.");

	// ---- Service-locator keys ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Move_Stamina, "DP.Service.Move.Stamina",
		"Service-locator key under which a UMove_StaminaComponent registers its ISeam_NeedProvider.");
}
