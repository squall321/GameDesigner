// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsProgressionModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsProgressionModule"

// Define the native anchor tags declared in DesignPatternsProgressionModule.h. Defined here in the
// module's primary translation unit so the hierarchy is registered exactly once at load.
namespace ProgTags
{
	UE_DEFINE_GAMEPLAY_TAG(Prog, "DP.Prog");
	UE_DEFINE_GAMEPLAY_TAG(Currency, "DP.Prog.Currency");
	UE_DEFINE_GAMEPLAY_TAG(Achievement, "DP.Prog.Achievement");
	UE_DEFINE_GAMEPLAY_TAG(Persist_Achievements, "DP.Prog.Persist.Achievements");
	UE_DEFINE_GAMEPLAY_TAG(Service_Analytics, "DP.Service.Wallet.Analytics");
	UE_DEFINE_GAMEPLAY_TAG(Service_PlatformTrophies, "DP.Service.Prog.PlatformTrophies");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.Prog");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BalanceChanged, "DP.Bus.Prog.BalanceChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AchievementProgress, "DP.Bus.Prog.AchievementProgress");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AchievementUnlocked, "DP.Bus.Prog.AchievementUnlocked");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ShopPurchased, "DP.Bus.Prog.Shop.Purchased");
}

void FDesignPatternsProgressionModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsProgression module started."));
}

void FDesignPatternsProgressionModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsProgression module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsProgressionModule, DesignPatternsProgression)

#undef LOCTEXT_NAMESPACE
