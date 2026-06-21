// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Character-locomotion module built ON TOP of the core "DesignPatterns" module's FSM + Action systems.
//
// Provides a data-driven movement state machine (UMove_MovementComponent : UDP_StateMachineComponent)
// whose states (Walk/Run/Sprint/Crouch/Slide/Jump/DoubleJump/Dash/WallRun/Climb/Mantle/Vault/Swim) wrap
// the engine UCharacterMovementComponent, a stamina need provider, an i-frame dash action and the
// trace-driven traversal queries. Siblings are reached ONLY through Seams interfaces (movement intent,
// stamina need) and the core message bus — never a concrete sibling header.
//
// To use it the host project must add a { "Name": "DesignPatternsMovement", "Type": "Runtime" } entry
// to the DesignPatterns.uplugin Modules array (already added here).
public class DesignPatternsMovement : ModuleRules
{
	public DesignPatternsMovement(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",            // ACharacter / UCharacterMovementComponent / water volumes
			"GameplayTags",
			"NetCore",           // push-model replication helpers for the stamina/intent components
			"DesignPatterns",    // core: FSM, actions, subsystem bases, message bus, native tags
			"DesignPatternsSeams" // movement-intent / need / sim-clock seams (TScriptInterface in headers)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// UMove_DeveloperSettings (a private settings type) derives from UDeveloperSettings; kept
			// private because no PUBLIC Movement header exposes a UDeveloperSettings-derived type on its API.
			"DeveloperSettings"
		});

		// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into CoreUObject
		// in 5.5; the Seams headers we include (FSeam_NetValue) pull it in, so gate the dependency.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
