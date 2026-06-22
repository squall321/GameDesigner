// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Camera/Seam_CinematicCameraSink.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_CinematicCameraSink. The Camera director overrides
 * these. The defaults fail safe: a sink that does not implement the override ignores the request
 * entirely (Begin returns an invalid handle; Update/End are no-ops), so a cutscene firing before a
 * local camera exists simply has no cosmetic effect rather than crashing.
 */

FGuid ISeam_CinematicCameraSink::BeginCinematicOverride_Implementation(FTransform /*POV*/, float /*FOV*/, float /*BlendInTime*/)
{
	return FGuid();
}

void ISeam_CinematicCameraSink::UpdateCinematicOverride_Implementation(FGuid /*Handle*/, FTransform /*POV*/, float /*FOV*/)
{
}

void ISeam_CinematicCameraSink::EndCinematicOverride_Implementation(FGuid /*Handle*/, float /*BlendOutTime*/)
{
}
