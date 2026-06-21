// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class DesignPatternsSampleTarget : TargetRules
{
	public DesignPatternsSampleTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("DesignPatternsSample");
	}
}
