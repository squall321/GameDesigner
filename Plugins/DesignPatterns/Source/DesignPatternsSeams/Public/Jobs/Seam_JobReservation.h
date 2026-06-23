// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Identity/Seam_EntityId.h"
#include "Seam_JobReservation.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_JobReservation : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared "single-claim reservation" seam. Lets agents (or any cross-module producer) claim a target
 * entity — a stockpile, a workstation, a resource node — so two agents never commit to the same target
 * at once, WITHOUT depending on the concrete reservation subsystem.
 *
 * This complements ISimAg_JobProvider: the job board hands out an abstract posting; the reservation seam
 * locks the concrete RESOURCE/SLOT a haul or job chain needs. A hauling chain reserves its source pile,
 * then its destination, before travelling, so a crowd does not all converge on one stockpile.
 *
 * HOUSE STYLE — BlueprintNativeEvent UINTERFACE (mirrors ISimAg_JobProvider): the implementer is a world
 * subsystem registered under a service key, resolved as `TScriptInterface<ISeam_JobReservation>` via the
 * service locator, and called through `Execute_` thunks.
 *
 * AUTHORITY — TryReserve / Release are authoritative concepts; the implementer guards authority
 * internally (the canonical reservation state lives on a replicated carrier) and is a no-op / returns
 * false on a pure client. IsReserved is a client-safe read of replicated state.
 */
class DESIGNPATTERNSSEAMS_API ISeam_JobReservation
{
	GENERATED_BODY()

public:
	/**
	 * Attempt to reserve Target for Agent. AUTHORITY ONLY in the implementer; returns false on clients.
	 * Succeeds only if Target is unreserved (or already reserved by Agent — re-reserving is idempotent).
	 * @return true if Agent now holds the reservation.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Jobs")
	bool TryReserve(FSeam_EntityId Target, FSeam_EntityId Agent);

	/**
	 * Release any reservation on Target. AUTHORITY ONLY; no-op on clients. Safe to call when Target is
	 * not reserved.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Jobs")
	void Release(FSeam_EntityId Target);

	/** True if Target currently holds a (non-expired) reservation. Client-safe read of replicated state. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Jobs")
	bool IsReserved(FSeam_EntityId Target) const;
};
