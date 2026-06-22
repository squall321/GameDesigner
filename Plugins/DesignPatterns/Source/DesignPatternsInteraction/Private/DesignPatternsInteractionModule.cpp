// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsInteractionModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace InteractNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Interact_Begin, "DP.Bus.Interact.Begin",
		"Broadcast on the server when an interaction has begun (BeginInteract succeeded).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Interact_Complete, "DP.Bus.Interact.Complete",
		"Broadcast on the server when an interaction completed normally.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Interact_Cancel, "DP.Bus.Interact.Cancel",
		"Broadcast on the server when an interaction was cancelled / interrupted.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Interact_Denied, "DP.Bus.Interact.Denied",
		"Broadcast on the server when an interaction request was re-validated and denied (verb unavailable).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Interact_BatchComplete, "DP.Bus.Interact.BatchComplete",
		"Broadcast on the server when a batch 'interact with all' request completed.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cmd_Interact, "DP.Cmd.Interact",
		"Identity of the optional undoable/replayable perform-interaction command.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cmd_BatchInteract, "DP.Cmd.BatchInteract",
		"Identity of the multi-target 'interact with all' batch command.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Interact_Verb, "DP.Data.Interact.Verb",
		"Data-asset identity root for interaction verb definitions; concrete verbs are children.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_Locked, "DP.Interact.Reason.Locked",
		"Availability reason: the interactable is locked.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_NoKey, "DP.Interact.Reason.NoKey",
		"Availability reason: the instigator lacks the required key/item.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_TooFar, "DP.Interact.Reason.TooFar",
		"Availability reason: the instigator is out of range.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_NotEnoughResource, "DP.Interact.Reason.NotEnoughResource",
		"Availability reason: the instigator lacks enough of a required resource/currency.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_OnCooldown, "DP.Interact.Reason.OnCooldown",
		"Availability reason: the verb is on cooldown.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reason_Full, "DP.Interact.Reason.Full",
		"Availability reason: the target/container is full.");
}

/**
 * Interaction module: interactable seam, interactor component, focus targeting, data-driven
 * verbs, optional interaction-as-command. Depends only on core + Seams.
 */
class FDesignPatternsInteractionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsInteraction module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsInteraction module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsInteractionModule, DesignPatternsInteraction)
