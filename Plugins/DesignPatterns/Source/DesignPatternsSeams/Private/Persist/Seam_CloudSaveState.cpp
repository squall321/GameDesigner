// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Persist/Seam_CloudSaveState.h"

/**
 * Default BlueprintNativeEvent bodies for the cloud-save store + conflict-resolver seams. The Platform
 * cloud subsystem overrides the store; projects supply the resolver. These fail-closed defaults treat
 * cloud as unavailable and defer all conflicts, so an unimplemented seam is safe to call.
 */

bool ISeam_CloudSaveStore::UploadSlotAsync_Implementation(const FString& /*SlotName*/)
{
	return false;
}

EPlat_CloudSyncState ISeam_CloudSaveStore::QueryRemoteState_Implementation(const FString& /*SlotName*/) const
{
	return EPlat_CloudSyncState::LocalOnly;
}

bool ISeam_CloudSaveStore::DetectConflict_Implementation(
	const FString& /*SlotName*/, FDateTime& OutLocalTime, FDateTime& OutRemoteTime) const
{
	OutLocalTime = FDateTime::MinValue();
	OutRemoteTime = FDateTime::MinValue();
	return false;
}

ESeam_CloudResolution ISeam_CloudConflictResolver::ResolveConflict_Implementation(
	const FString& /*SlotName*/, FDateTime /*LocalTime*/, FDateTime /*RemoteTime*/) const
{
	// Defer by default: a project that wants automatic resolution overrides this.
	return ESeam_CloudResolution::AskLater;
}
