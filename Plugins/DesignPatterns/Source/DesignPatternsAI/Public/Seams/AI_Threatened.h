// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_Threatened.generated.h"

/**
 * Threat seam: the aggro contract a threat-tracking agent exposes.
 *
 * Implemented by UAI_ThreatComponent. Combat / abilities / scripting credit threat against an
 * agent through THIS interface (resolved via GetComponentByInterface or the service locator),
 * without depending on the AI module's concrete threat-table type, and the brain / squad logic
 * reads the current top threat to pick a target — again without a concrete dependency.
 *
 * AUTHORITY: AddThreat is AUTHORITY-ONLY (the aggro table is authoritative gameplay state); the
 * implementation guards it at the top and no-ops on clients. Only the resulting top-threat entity
 * id replicates to clients. GetTopThreat() is safe to read on any machine.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UAI_Threatened : public UInterface
{
	GENERATED_BODY()
};

/** @see UAI_Threatened */
class DESIGNPATTERNSAI_API IAI_Threatened
{
	GENERATED_BODY()

public:
	/**
	 * @return the entity currently holding the most threat against this agent, or an invalid id
	 * when the aggro table is empty. Safe to read on any machine (top threat replicates).
	 */
	virtual FSeam_EntityId GetTopThreat() const = 0;

	/**
	 * Add Amount of threat from Source, classified by ThreatTag. AUTHORITY ONLY (no-op on clients).
	 *
	 * Accumulates into the aggro table (subject to per-entry decay) and recomputes the top threat;
	 * if the top threat changes the implementation replicates it and broadcasts DP.Bus.AI.ThreatChanged.
	 *
	 * @param Source    the entity to credit the threat to.
	 * @param Amount    threat magnitude to add (negative values reduce threat, clamped at zero).
	 * @param ThreatTag classification of the threat (e.g. a damage-type or role tag) for weighting.
	 */
	virtual void AddThreat(FSeam_EntityId Source, float Amount, FGameplayTag ThreatTag) = 0;
};
