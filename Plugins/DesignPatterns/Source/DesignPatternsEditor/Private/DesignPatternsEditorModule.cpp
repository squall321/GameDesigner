// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

#if WITH_DP_GAMEPLAY_DEBUGGER
#include "GameplayDebugger.h"
#include "DPGameplayDebuggerCategory.h"
#endif

/**
 * Editor-only (UncookedOnly) module: gameplay-debugger category, component visualizers,
 * data validation and details customizations for the DesignPatterns runtime types.
 */
class FDesignPatternsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsEditor module started."));

#if WITH_DP_GAMEPLAY_DEBUGGER
		IGameplayDebugger& GameplayDebugger = IGameplayDebugger::Get();
		GameplayDebugger.RegisterCategory(
			GameplayDebuggerCategoryName,
			IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_DP::MakeInstance),
			EGameplayDebuggerCategoryState::EnabledInGameAndSimulate);
		GameplayDebugger.NotifyCategoriesChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_DP_GAMEPLAY_DEBUGGER
		if (IGameplayDebugger::IsAvailable())
		{
			IGameplayDebugger& GameplayDebugger = IGameplayDebugger::Get();
			GameplayDebugger.UnregisterCategory(GameplayDebuggerCategoryName);
			GameplayDebugger.NotifyCategoriesChanged();
		}
#endif

		UE_LOG(LogDP, Log, TEXT("DesignPatternsEditor module shut down."));
	}

private:
#if WITH_DP_GAMEPLAY_DEBUGGER
	/** Stable name used to register/unregister the DesignPatterns gameplay-debugger category. */
	static const FName GameplayDebuggerCategoryName;
#endif
};

#if WITH_DP_GAMEPLAY_DEBUGGER
const FName FDesignPatternsEditorModule::GameplayDebuggerCategoryName = TEXT("DesignPatterns");
#endif

IMPLEMENT_MODULE(FDesignPatternsEditorModule, DesignPatternsEditor)
