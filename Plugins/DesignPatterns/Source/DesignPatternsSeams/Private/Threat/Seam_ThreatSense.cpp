// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Threat/Seam_ThreatSense.h"

// INERT native default for the threat-sense seam. With no threat source present (the seam unresolved or
// an implementer that doesn't override), QueryThreat always reports "no threat", so a flee/interrupt
// behaviour simply never triggers from this source. A combat / hazard module overrides it to report real
// dangers. The out-params are left untouched on the "no threat" path, as documented.

bool ISeam_ThreatSense::QueryThreat_Implementation(
	FVector /*At*/, float /*Radius*/, FVector& /*OutThreatLoc*/, float& /*OutSeverity*/) const
{
	return false;
}
