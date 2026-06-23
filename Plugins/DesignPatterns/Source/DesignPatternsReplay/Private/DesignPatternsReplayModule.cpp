// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsReplayModule.h"
#include "Core/DPLog.h"

namespace Rep_NativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Replay, "DP.Service.Replay",
		"Service-locator key for the replay subsystem's IRep_ReplayEventSource aggregate.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_SpectatorCamera, "DP.Service.Replay.SpectatorCamera",
		"Service-locator key the game/Camera module registers an IRep_SpectatorCamera under.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Replay_Highlights, "DP.Service.Replay.Highlights",
		"Service-locator key the highlight subsystem registers itself (as an IRep_ReplayEventSource) under.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Replay_Thumbnail, "DP.Service.Replay.Thumbnail",
		"Service-locator key a thumbnail-capture adapter registers an ISeam_ReplayThumbnailSource under.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay, "DP.Bus.Replay",
		"Root of the message-bus subtree the replay timeline subscribes to for significant events.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_RecordingStarted, "DP.Bus.Replay.RecordingStarted",
		"Broadcast when replay recording starts; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_RecordingStopped, "DP.Bus.Replay.RecordingStopped",
		"Broadcast when replay recording stops; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_PlaybackStarted, "DP.Bus.Replay.PlaybackStarted",
		"Broadcast when replay playback starts; payload carries the replay name.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_Death, "DP.Bus.Replay.Death",
		"Broadcast locally when the killcam component observes its owner's death.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Replay_HighlightDetected, "DP.Bus.Replay.HighlightDetected",
		"Broadcast when the highlight detector promotes a window into a saved highlight moment.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event, "Rep.Event",
		"Identity root for replay timeline marker events.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Bookmark, "Rep.Event.Bookmark",
		"A manual/scripted bookmark marker dropped onto the replay timeline.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Command, "Rep.Event.Command",
		"A gameplay command recorded into the timeline from the core command history.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_BusMessage, "Rep.Event.BusMessage",
		"A message-bus event promoted into the replay timeline.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Chapter, "Rep.Event.Chapter",
		"A coarse chapter / section marker on the replay timeline.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Death, "Rep.Event.Death",
		"A death event recorded onto the timeline (drives killcam framing and highlight scoring).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Highlight, "Rep.Highlight",
		"Identity root for promoted highlight markers (distinct from the ingested Rep.Event.* root).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Highlight_MultiKill, "Rep.Highlight.MultiKill",
		"A multi-kill highlight: several scoring events inside the rule-set window.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Highlight_Clutch, "Rep.Highlight.Clutch",
		"A clutch highlight: a decisive event late in / against the odds of an encounter.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Highlight_Objective, "Rep.Highlight.Objective",
		"An objective highlight: a captured/completed objective worth surfacing.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_HighlightDetected, "Rep.Analytics.HighlightDetected",
		"Aggregate analytics event recorded when a highlight is detected (count + score attrs).");
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
