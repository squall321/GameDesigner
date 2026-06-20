// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Advanced AI + spawn director: perception adapter, behavior bridge (FSM or BehaviorTree), squad tactics,
// budget/wave spawn director. Builds on core FSM/Strategy + SimAgents brain; World/SimAgents are PRIVATE
// (resolved via the service locator) so AI stays independently removable. Genre modules are never linked.
public class DesignPatternsAI : ModuleRules
{
	public DesignPatternsAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"AIModule", "GameplayTasks", "NavigationSystem",
			"DesignPatternsWorld",      // private: IWorldHub_Queryable resolved in .cpp
			"DesignPatternsSimAgents"   // private: brain/agent seams resolved in .cpp
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
