// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Replay module: a clean, tag/metadata-driven facade over the ENGINE demo driver
// (UGameInstance::StartRecordingReplay / StopRecordingReplay / PlayReplay and the network
// replay streamer). It does NOT reinvent networking — it records, enumerates and plays the
// engine's own demo files and layers a gameplay-event timeline + scrubber view-model + a
// lightweight spectator/free-cam on top, coupling to other modules only through seams.
public class DesignPatternsReplay : ModuleRules
{
	public DesignPatternsReplay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			// Engine + tags + net core for the demo driver, plus the runtime core and shared seams.
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams",
			// MVVM base (UDP_ViewModelBase) lives here; the scrubber view-model derives from it.
			"DesignPatternsUI"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// UDeveloperSettings base for Rep_DeveloperSettings.
			"DeveloperSettings",
			// The network replay streamer interface (EnumerateStreams / FNetworkReplayStreamInfo)
			// used to build the recorded-replay registry from the on-disk demos.
			"NetworkReplayStreaming"
		});

		// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4; merged into CoreUObject in 5.5.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
