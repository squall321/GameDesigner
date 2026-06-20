// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InvUI_ContainerAccess.generated.h"

class AActor;

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "InvUI Container Access"))
class UInvUI_ContainerAccess : public UInterface
{
	GENERATED_BODY()
};

/**
 * SERVER-SIDE access gate for a container.
 *
 * Optional companion to IInvUI_ItemContainer. When a backend implements it, the server-side
 * intent path calls CanAccess(Requestor) *before* applying any mutation derived from a UI
 * intent — so opening/looting a chest can require proximity, ownership, a quest flag, a lock
 * being picked, etc. This is purely an authority-side check: it must never be consulted to
 * decide what the client UI shows (that is what the IInvUI_ItemContainer capability tags are
 * for); it exists to stop a malicious client from acting on a container it should not reach.
 *
 * Returning true means "this requestor may act on this container right now". A backend that
 * does not implement this interface is treated as unconditionally accessible by the
 * server-side router (the router still performs its own authority and target re-derivation).
 */
class IInvUI_ContainerAccess
{
	GENERATED_BODY()

public:
	/**
	 * Authority-only: may Requestor act on this container at this moment?
	 *
	 * @param Requestor The actor whose player issued the intent (pawn/controller/owner), as
	 *                  re-derived on the server — never a client-supplied pointer.
	 * @return True to allow the pending mutation; false to reject it silently.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Access")
	bool CanAccess(AActor* Requestor) const;
};
