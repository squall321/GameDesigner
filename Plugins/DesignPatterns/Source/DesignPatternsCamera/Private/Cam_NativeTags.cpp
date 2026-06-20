// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Cam_NativeTags.h"

namespace Cam_NativeTags
{
	// Camera mode identity root + shipped concrete modes.
	UE_DEFINE_GAMEPLAY_TAG(Mode, "Cam.Mode");
	UE_DEFINE_GAMEPLAY_TAG(Mode_ThirdPerson, "Cam.Mode.ThirdPerson");
	UE_DEFINE_GAMEPLAY_TAG(Mode_FirstPerson, "Cam.Mode.FirstPerson");
	UE_DEFINE_GAMEPLAY_TAG(Mode_TopDown, "Cam.Mode.TopDown");
	UE_DEFINE_GAMEPLAY_TAG(Mode_Orbit, "Cam.Mode.Orbit");
	UE_DEFINE_GAMEPLAY_TAG(Mode_Fixed, "Cam.Mode.Fixed");

	// Service-locator key for the camera-mode provider seam.
	UE_DEFINE_GAMEPLAY_TAG(Service_ModeProvider, "DP.Service.Camera.ModeProvider");

	// Input-mode tag pushed for look-capturing modes.
	UE_DEFINE_GAMEPLAY_TAG(InputMode, "Cam.InputMode.LookCapture");

	// ---- Targeting / lock-on ----
	UE_DEFINE_GAMEPLAY_TAG(Service_TargetSource, "DP.Service.Camera.TargetSource");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_LockOn, "Cam.InputMode.LockOn");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TargetChanged, "DP.Bus.Camera.TargetChanged");
}
