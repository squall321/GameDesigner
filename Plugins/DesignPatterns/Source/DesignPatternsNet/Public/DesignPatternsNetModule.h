// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime networking module for the DesignPatterns plugin.
 *
 * Builds ON TOP of the core "DesignPatterns" module, extending the authority/replication
 * patterns it establishes. Provides: an optional Online Subsystem session wrapper
 * (UNet_SessionSubsystem), authority/role helper library (UNet_NetUtilsLibrary), quantized
 * replication helper structs (FNet_RepFloat / FNet_RepInt), and a canonical Server-RPC ->
 * validate -> apply -> replicate demonstration component (UNet_AuthorityComponent).
 */
class FDesignPatternsNetModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/** Convenience accessor for the loaded module. */
	static FDesignPatternsNetModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FDesignPatternsNetModule>("DesignPatternsNet");
	}

	/** Returns true if the module is currently loaded. */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("DesignPatternsNet");
	}
};
