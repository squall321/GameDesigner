// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsReplayModule.h"
#include "Core/DPLog.h"

namespace Rep_NativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Replay, "DP.Service.Replay",
		"Service-locator key for the replay subsystem's IRep_ReplayEventSource aggregate.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_SpectatorCamera, "DP.Service.Replay.SpectatorCamera",
		"Service-locator key the game/Camera module registers an IRep_SpectatorCamera under.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay, "DP.Bus.Replay",
		"Root of the message-bus subtree the replay timeline subscribes to for significant events.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_RecordingStarted, "DP.Bus.Replay.RecordingStarted",
		"Broadcast when replay recording starts; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_RecordingStopped, "DP.Bus.Replay.RecordingStopped",
		"Broadcast when replay recording stops; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_PlaybackStarted, "DP.Bus.Replay.PlaybackStarted",
		"Broadcast when replay playback starts; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event, "Rep.Event",
		"Identity root for replay timeline marker events.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bookmark, "Rep.Event.Bookmark",
		"A manual/scripted bookmark marker dropped onto the replay timeline.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Command, "Rep.Event.Command",
		"A gameplay command recorded into the timeline from the core command history.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_BusMessage, "Rep.Event.BusMessage",
		"A message-bus event promoted into the replay timeline.");
}

void FDesignPatternsReplayModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsReplay module started."));
}

void FDesignPatternsReplayModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsReplay module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsReplayModule, DesignPatternsReplay)
