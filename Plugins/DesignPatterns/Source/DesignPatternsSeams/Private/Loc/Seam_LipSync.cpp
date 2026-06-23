// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loc/Seam_LipSync.h"

// INERT native defaults for the lip-sync seam. A project with no facial-animation backend leaves these
// unoverridden, so the voice subsystem's lip-sync calls become harmless no-ops (no visemes, no errors).
// The real project-side adapter overrides all three to forward to its concrete anim/MetaHuman driver.

void ISeam_LipSync::BeginLipSync_Implementation(FGameplayTag /*Speaker*/, USoundBase* /*Vo*/, UObject* /*CurveAsset*/)
{
}

void ISeam_LipSync::PushViseme_Implementation(FGameplayTag /*Speaker*/, float /*Time*/, uint8 /*Viseme*/)
{
}

void ISeam_LipSync::EndLipSync_Implementation(FGameplayTag /*Speaker*/)
{
}
