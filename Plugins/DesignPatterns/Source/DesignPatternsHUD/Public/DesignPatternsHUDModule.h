// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * DesignPatternsHUD module interface.
 *
 * HUD framework + advanced UI + input context: data-driven HUD layout, notification/toast queue,
 * minimap/markers, menu navigation stack and Enhanced Input context layering. Built on the UI/MVVM
 * module (UDP_ViewModelBase / UDP_ViewBase / UDP_UILayoutSubsystem) and the Platform input device
 * seam. The HUD is purely LOCAL/COSMETIC: it is a projection of already-replicated gameplay
 * surfaced through the core message bus (DP.Bus.*) and never replicates its own state.
 *
 * NOTE: this module's native GameplayTags live in HUD_NativeTags.h (namespace HUDTags) — the single
 * tag registry for the whole HUD module. New tags are added there, not here.
 */
class FDesignPatternsHUDModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
