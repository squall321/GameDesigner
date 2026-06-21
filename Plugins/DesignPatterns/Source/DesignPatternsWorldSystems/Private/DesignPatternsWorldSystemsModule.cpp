// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsWorldSystemsModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsWorldSystemsModule"

// Define the native anchor tags declared in DesignPatternsWorldSystemsModule.h. Defined here in the
// module's primary translation unit so the hierarchy is registered exactly once at load.
namespace WorldSystemsNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG(WS, "WS");
	UE_DEFINE_GAMEPLAY_TAG(Weather, "WS.Weather");
	UE_DEFINE_GAMEPLAY_TAG(Weather_State, "WS.Weather.State");
	UE_DEFINE_GAMEPLAY_TAG(Vfx, "WS.Vfx");
	UE_DEFINE_GAMEPLAY_TAG(Service_Weather, "DP.Service.Weather");
	UE_DEFINE_GAMEPLAY_TAG(Service_Vfx, "DP.Service.Vfx");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.WorldSystems");
	UE_DEFINE_GAMEPLAY_TAG(Bus_WeatherChanged, "DP.Bus.WorldSystems.WeatherChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RequestWeather, "DP.Bus.WorldSystems.RequestWeather");
}

void FDesignPatternsWorldSystemsModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsWorldSystems module started."));
}

void FDesignPatternsWorldSystemsModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsWorldSystems module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsWorldSystemsModule, DesignPatternsWorldSystems)

#undef LOCTEXT_NAMESPACE
