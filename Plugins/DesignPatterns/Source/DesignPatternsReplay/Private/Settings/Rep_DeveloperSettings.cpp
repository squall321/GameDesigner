// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"

URep_DeveloperSettings::URep_DeveloperSettings()
{
	// Default the timeline bus root to this module's DP.Bus.Replay anchor so projects opt events in
	// by broadcasting under it. Games can widen to DP.Bus to harvest everything.
	TimelineBusRoot = Rep_NativeTags::Bus_Replay;
}

const URep_DeveloperSettings* URep_DeveloperSettings::Get()
{
	return GetDefault<URep_DeveloperSettings>();
}
