// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DragDrop/DPDropZone.h"

bool IDP_DropZone::CanAcceptPayload_Implementation(const FDP_DragPayload& Payload) const
{
	if (!Payload.PayloadType.IsValid())
	{
		return false;
	}

	// Default policy: accept when the payload type matches (exactly or as a child of) one of the
	// zone's accepted types. Hierarchical matching lets a zone accept "DP.UI.Drag.Item" and its
	// children. We must resolve the accepted set through the interface thunk so a Blueprint override
	// of GetAcceptedPayloadTypes is honoured.
	const UObject* SelfObject = _getUObject();
	if (!SelfObject)
	{
		return false;
	}

	const FGameplayTagContainer Accepted = IDP_DropZone::Execute_GetAcceptedPayloadTypes(SelfObject);
	return Accepted.HasTag(Payload.PayloadType);
}
