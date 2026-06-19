// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatterns plugin.
 *
 * These are ROOT/anchor tags only — concrete channels, states and command kinds are
 * authored by the game project as child tags (in the project's tag config or its own
 * native tags). Anchoring the roots here guarantees the hierarchy exists at startup so
 * tag-hierarchy matching (e.g. a listener on `DP.Bus` receiving `DP.Bus.Combat.Damage`)
 * always works.
 */
namespace DPNativeTags
{
	// Message bus channel roots.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	// Object pool category root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pool);

	// Finite state machine state root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FSM);

	// Command kind/category root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cmd);

	// Service locator key root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service);

	// Data registry identity root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data);

	// UI screen/layer root.
	DESIGNPATTERNS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI);
}
