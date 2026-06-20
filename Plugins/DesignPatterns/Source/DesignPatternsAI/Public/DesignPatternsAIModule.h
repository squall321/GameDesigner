// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Advanced-AI runtime module for the DesignPatterns plugin.
 *
 * This module provides the perception adapter, behavior bridge, brain seam and threat table (this
 * area), plus the squad and spawn-director areas, sitting ON TOP of the core "DesignPatterns" module
 * (FSM, Strategy, message bus, blackboard, service locator, data registry, logging) and the shared
 * "DesignPatternsSeams" contracts (FSeam_EntityId, ISeam_EntityIdentity, FSeam_NetValue,
 * ISeam_InputModeArbiter, ISeam_SimClock).
 *
 * WRAP, DON'T REINVENT: the perception adapter wraps the engine AIModule's UAIPerceptionComponent
 * and the behavior bridge can drive either a core UDP_StateMachineComponent or an engine
 * UBehaviorTree (via an AAIController). The engine dependencies (AIModule, NavigationSystem) are
 * PRIVATE so consumers of this module's PUBLIC headers never inherit them.
 *
 * COUPLING: World and SimAgents are reached ONLY through seams / the service locator resolved in
 * .cpp; their public headers are never included from THIS module's public headers. Cross-module
 * traffic flows through the message bus (DP.Bus.AI.* / DP.Bus.Combat.* tags) and the seam interfaces
 * (IAI_Brain, IAI_Threatened, ISeam_EntityIdentity, IDP_BlackboardProvider). Native tags for the
 * module live in DesignPatternsAINativeTags.h.
 */
class FDesignPatternsAIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
