// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsUI : ModuleRules
{
	public DesignPatternsUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"UMG",
			"Slate",
			"SlateCore",
			"FieldNotification",   // INotifyFieldValueChanged backing for the lite ViewModel
			"DesignPatterns"       // core: hosts the message bus this module subscribes to
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"DeveloperSettings",
			"Projects"
		});

		// Optional UI plugins. They are declared with "Optional": true in the .uplugin, so
		// the host project only needs them present to light up the richer paths. We default
		// the feature defines on; a host that does not ship these plugins should set them to
		// 0 in its own Target.cs, or remove the optional plugin references from the .uplugin.
		//
		// ModelViewViewModel: richer field-binding path. When absent, the hand-rolled lite
		// ViewModel (built on FieldNotification, which ships with the engine) is used instead.
		if (Target.bBuildAllModules || true)
		{
			// Kept simple and portable: rely on the optional plugin reference in the .uplugin.
			// WITH_DP_MVVM / WITH_DP_COMMONUI gate the optional code paths at compile time.
			PublicDefinitions.Add("WITH_DP_MVVM=0");
			PublicDefinitions.Add("WITH_DP_COMMONUI=0");
		}
	}
}
