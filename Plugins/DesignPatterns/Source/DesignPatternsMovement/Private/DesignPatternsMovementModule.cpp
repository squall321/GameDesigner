// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

/**
 * Runtime character-locomotion module for the DesignPatterns plugin.
 *
 * Builds on the core FSM (UDP_StateMachineComponent / UDP_State / UDP_StateMachineDefinition) and the
 * lightweight Action system to provide a data-driven movement state machine wrapping the engine
 * UCharacterMovementComponent: ground/air/traversal/water states, a stamina need provider, an i-frame
 * dash action, and trace-driven mantle/vault/ledge queries. Cross-module coupling is exclusively through
 * Seams interfaces (movement intent, need, sim clock) and the core message bus.
 *
 * Native gameplay tags are registered automatically by the engine's NativeGameplayTags machinery from
 * the UE_DEFINE_GAMEPLAY_TAG_COMMENT entries in Move_NativeTags.cpp; no explicit registration is needed
 * here. Startup/shutdown only log lifecycle.
 */
class FDesignPatternsMovementModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsMovement module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsMovement module shut down."));
	}
};

IMPLEMENT_MODULE(FDesignPatternsMovementModule, DesignPatternsMovement)
