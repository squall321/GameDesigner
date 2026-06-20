// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_Squad.generated.h"

/**
 * Read seam onto a tactical squad. Implemented by UAI_SquadSubsystem and resolved by consumers as a
 * TScriptInterface<IAI_Squad> from the service locator (DP.Service.AI.Squad) — so other modules read
 * squad membership/roles/formation without ever depending on the concrete subsystem or carrier type.
 *
 * The interface is deliberately a thin READ surface keyed by a stable FSeam_EntityId. Authoritative
 * squad MUTATION (claim a role, assign a slot, add/remove members) lives on the replicated carrier
 * (AAI_SquadCarrier) and the subsystem's authority-guarded API, never here — exactly mirroring the
 * world-hub "read interface vs. authority mutators" split.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UAI_Squad : public UInterface
{
	GENERATED_BODY()
};

/** @see UAI_Squad */
class DESIGNPATTERNSAI_API IAI_Squad
{
	GENERATED_BODY()

public:
	/**
	 * @return the stable id of the squad this provider represents. Invalid if the provider currently
	 * backs no live squad (e.g. the subsystem resolved before any squad was formed). Callers should
	 * treat an invalid id as "no squad".
	 */
	virtual FGuid GetSquadId() const = 0;

	/**
	 * @return the tactical role currently assigned to Member (e.g. a project tag like
	 * AI.Role.Leader / AI.Role.Flanker), or an empty tag if Member is not in this squad or has no
	 * role yet. Roles are designer/game tags; this module anchors none of them.
	 */
	virtual FGameplayTag GetRole(FSeam_EntityId Member) const = 0;

	/**
	 * @return the world-space formation slot transform assigned to Member relative to the squad's
	 * current anchor, or FTransform::Identity if Member has no slot. The squad subsystem keeps slots
	 * in sync with the formation shape; movers read this to position themselves.
	 */
	virtual FTransform GetFormationSlot(FSeam_EntityId Member) const = 0;
};
