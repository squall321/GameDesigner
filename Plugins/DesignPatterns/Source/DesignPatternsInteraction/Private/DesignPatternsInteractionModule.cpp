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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cmd_Interact, "DP.Cmd.Interact",
		"Identity of the optional undoable/replayable perform-interaction command.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Interact_Verb, "DP.Data.Interact.Verb",
		"Data-asset identity root for interaction verb definitions; concrete verbs are children.");
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
