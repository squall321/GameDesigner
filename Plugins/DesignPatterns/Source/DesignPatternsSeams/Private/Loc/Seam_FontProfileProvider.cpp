// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loc/Seam_FontProfileProvider.h"

// INERT native defaults for the font-profile seam. A project with no Localization font subsystem leaves
// these unoverridden, so UI resolves empty soft refs (use the engine default font) and left-to-right
// layout. The real implementer (ULoc_FontSubsystem in DesignPatternsLocalization) overrides all three.

TSoftObjectPtr<UObject> ISeam_FontProfileProvider::GetCultureFontFace_Implementation(FGameplayTag /*Role*/) const
{
	return TSoftObjectPtr<UObject>();
}

TArray<TSoftObjectPtr<UObject>> ISeam_FontProfileProvider::GetFallbackFontFaces_Implementation() const
{
	return TArray<TSoftObjectPtr<UObject>>();
}

bool ISeam_FontProfileProvider::IsRightToLeft_Implementation() const
{
	return false;
}
