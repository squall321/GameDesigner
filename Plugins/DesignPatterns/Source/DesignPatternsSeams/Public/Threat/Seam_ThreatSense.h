// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_ThreatSense.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ThreatSense : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared "is there a threat near here?" read seam. Lets a flee/interrupt behaviour ask whether a danger
 * exists at a location WITHOUT depending on any combat / survival / predator module — the threat source
 * (a combat encounter system, a hazard volume, a predator AI) implements this and the agent only sees
 * the seam.
 *
 * HOUSE STYLE — BlueprintNativeEvent UINTERFACE: the implementer is typically a world subsystem
 * registered under a service key and resolved as `TScriptInterface<ISeam_ThreatSense>`, called through
 * the generated `Execute_` thunk. This is a project-supplied bridge, hence native-event.
 *
 * THREAD / AUTHORITY — QueryThreat is a const read, safe on server and clients; it never mutates state.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ThreatSense
{
	GENERATED_BODY()

public:
	/**
	 * Query for the most relevant threat within Radius of At.
	 *
	 * @param At            World location to test around.
	 * @param Radius        Search radius (world units). Non-positive radii are treated as "point query".
	 * @param OutThreatLoc  Set to the threat's world location when one is found (so a flee behaviour can
	 *                      steer away from it). Left unchanged when none is found.
	 * @param OutSeverity   Set to a normalized [0,1] severity when a threat is found (1 = maximal danger),
	 *                      so a consumer can scale its reaction. Left unchanged when none is found.
	 * @return true when a threat was found within Radius (OutThreatLoc / OutSeverity then valid).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Threat")
	bool QueryThreat(FVector At, float Radius, FVector& OutThreatLoc, float& OutSeverity) const;
};
