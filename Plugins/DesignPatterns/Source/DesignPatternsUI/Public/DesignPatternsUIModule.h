// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * UI module for the DesignPatterns plugin: MVVM ViewModel bases, view bases,
 * the UI mediator/layout subsystems and the message-bus listener adapter.
 * Depends on the runtime core module (which hosts the shared message bus).
 */
class FDesignPatternsUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
