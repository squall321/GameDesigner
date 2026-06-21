// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Identity/Seam_EntityId.h"
#include "Seam_TauntSink.generated.h"

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USeam_TauntSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * Taunt seam: a sink that can be FORCED to focus a given source for a duration. Implemented by
 * UAI_ThreatShareComponent (which composes the agent's threat table) and resolved as a
 * TScriptInterface<ISeam_TauntSink> — typically off the target actor via FindComponentByInterface.
 *
 * This lets combat / abilities apply a "taunt" (forced aggro) WITHOUT depending on the AI module's
 * concrete threat-share component, and without re-implementing the threat table.
 *
 * AUTHORITY: ForceTaunt is AUTHORITY-ONLY (forced aggro is authoritative gameplay state); the
 * implementer guards it at the top and no-ops on clients. The resulting forced-target id replicates so
 * clients can react cosmetically.
 */
class DESIGNPATTERNSSEAMS_API ISeam_TauntSink
{
	GENERATED_BODY()

public:
	/**
	 * Force this sink to treat Source as its top threat for Duration seconds. AUTHORITY ONLY.
	 *
	 * @param Source   Stable id of the entity that should become the forced target.
	 * @param Duration How long the forced taunt lasts (seconds). <= 0 clears any active taunt.
	 */
	virtual void ForceTaunt(FSeam_EntityId Source, float Duration) = 0;
};
