// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/Seam_StreamingControl.h"

// Fail-READY native defaults for the streaming-control seam. A provider that does not override these
// reports "nothing pending / fully ready" so the loading flow never stalls waiting on an inert adapter;
// the real LevelDirector adapter overrides all four to wrap the engine's ULevelStreaming state.

void ISeam_StreamingControl::RequestLevelsResident_Implementation(const FGameplayTagContainer& /*Categories*/)
{
}

float ISeam_StreamingControl::GetAggregateProgress_Implementation() const
{
	return 1.f;
}

bool ISeam_StreamingControl::AreRequestedLevelsReady_Implementation() const
{
	return true;
}

void ISeam_StreamingControl::ReleaseRequest_Implementation()
{
}
