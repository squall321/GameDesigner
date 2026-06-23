// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsModContentModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsModContentModule"

// Define the native anchor tags declared in DesignPatternsModContentModule.h. Defined here in the
// module's primary translation unit so the hierarchy is registered exactly once at load.
namespace ModTags
{
	UE_DEFINE_GAMEPLAY_TAG(Mod, "DP.Mod");
	UE_DEFINE_GAMEPLAY_TAG(Pack, "DP.Mod.Pack");
	UE_DEFINE_GAMEPLAY_TAG(Source, "DP.Mod.Source");
	UE_DEFINE_GAMEPLAY_TAG(Service_ModContent, "DP.Service.ModContent");
	UE_DEFINE_GAMEPLAY_TAG(Service_Validator, "DP.Service.Mod.Validator");
	UE_DEFINE_GAMEPLAY_TAG(Service_Source, "DP.Service.Mod.Source");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.Mod");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Mounted, "DP.Bus.Mod.Mounted");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Unmounted, "DP.Bus.Mod.Unmounted");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Rejected, "DP.Bus.Mod.Rejected");

	// Additive deepening anchors.
	UE_DEFINE_GAMEPLAY_TAG(Service_ModSignature, "DP.Service.Mod.Signature");
	UE_DEFINE_GAMEPLAY_TAG(Service_ModCatalog, "DP.Service.Mod.Catalog");
	UE_DEFINE_GAMEPLAY_TAG(Service_ModResolution, "DP.Service.Mod.Resolution");
	UE_DEFINE_GAMEPLAY_TAG(Bus_SettingsChanged, "DP.Bus.Mod.SettingsChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_CatalogChanged, "DP.Bus.Mod.CatalogChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_HotReloaded, "DP.Bus.Mod.HotReloaded");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TrustRejected, "DP.Bus.Mod.TrustRejected");
	UE_DEFINE_GAMEPLAY_TAG(Persist, "DP.Persist.Mod");
	UE_DEFINE_GAMEPLAY_TAG(Persist_PackConfig, "DP.Persist.Mod.PackConfig");
	UE_DEFINE_GAMEPLAY_TAG(Reason_Untrusted, "DP.Mod.Reason.Untrusted");
	UE_DEFINE_GAMEPLAY_TAG(Reason_HashMismatch, "DP.Mod.Reason.HashMismatch");
	UE_DEFINE_GAMEPLAY_TAG(Reason_TagConflict, "DP.Mod.Reason.TagConflict");
}

void FDesignPatternsModContentModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsModContent module started."));
}

void FDesignPatternsModContentModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsModContent module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsModContentModule, DesignPatternsModContent)

#undef LOCTEXT_NAMESPACE
