// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Display/Seam_FloatingTextFeed.h"

// INERT native default for the floating-text feed seam. With no HUD overlay registered the push is a
// harmless no-op, so a project with no floating-text presentation still links and producers (Combat,
// the Replay killcam) simply discard their events. The real consumer is the HUD's floating-text
// overlay widget, which overrides PushFloatingText to enqueue a world-anchored popup.

void ISeam_FloatingTextFeed::PushFloatingText_Implementation(
	FSeam_EntityId /*Anchor*/, FText /*Text*/, FGameplayTag /*StyleTag*/, FSeam_NetValue /*Magnitude*/)
{
	// Default: no overlay -> nothing to display.
}
