// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loc/Seam_AccessibilityConsumer.h"

void ISeam_AccessibilityConsumer::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& /*Options*/)
{
	// Inert default: an unoverridden consumer does nothing on option changes.
}
