// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * Conventional service-locator keys for the SaveSystem area.
 *
 * The SaveSystem slot-policy implementation (whoever owns the slot bookkeeping) is expected to register
 * itself under ServiceKey_SaveSlotManager so the checkpoint/autosave area and the save/load UI can resolve
 * the ISeam_SaveSlotManager seam without hard-coupling to a concrete class. These are looked up by string at
 * runtime (ErrorIfNotFound=false) so a project that has not registered the service simply degrades to the
 * documented inert default rather than asserting.
 */
namespace SaveX_ServiceKeys
{
	/** Stable key under which the save-slot policy seam (ISeam_SaveSlotManager) is published. */
	FORCEINLINE FGameplayTag SlotManager()
	{
		static const FGameplayTag Key =
			FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Save.SlotManager")), /*ErrorIfNotFound=*/false);
		return Key;
	}
}
