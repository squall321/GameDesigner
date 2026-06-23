// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Persist/Seam_Persistable.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_Persistable. The seam carries no behaviour of its own —
 * each save participant overrides these. These inert defaults capture/restore nothing and report an
 * invalid kind, so an object that presents the interface without overriding still links and is safe.
 */

void ISeam_Persistable::CaptureState_Implementation(FInstancedStruct& /*Out*/) const
{
}

void ISeam_Persistable::RestoreState_Implementation(const FInstancedStruct& /*In*/)
{
}

FGameplayTag ISeam_Persistable::GetPersistenceKind_Implementation() const
{
	return FGameplayTag();
}
