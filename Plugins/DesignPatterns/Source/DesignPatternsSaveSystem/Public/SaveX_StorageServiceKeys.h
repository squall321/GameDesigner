// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * Conventional service-locator keys for the additive SaveSystem STORAGE layer.
 *
 * A project publishes its ISeam_SaveCipher under Cipher() and its cloud store seam under Cloud() so the
 * storage subsystem and cloud bridge can resolve them by stable tag without hard-coupling to a concrete
 * class. Looked up with ErrorIfNotFound=false so an unregistered service degrades to the documented inert
 * default (plaintext saves / no cloud upload) rather than asserting.
 *
 * Mirrors SaveX_ServiceKeys exactly (header-only FORCEINLINE accessors), and is a sibling of it so the
 * original SlotManager() key stays where it is.
 */
namespace SaveX_StorageServiceKeys
{
	/** Stable key under which the byte-buffer cipher seam (ISeam_SaveCipher) is published. */
	FORCEINLINE FGameplayTag Cipher()
	{
		static const FGameplayTag Key =
			FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Save.Cipher")), /*ErrorIfNotFound=*/false);
		return Key;
	}

	/** Stable key under which the cloud save store seam is published. */
	FORCEINLINE FGameplayTag Cloud()
	{
		static const FGameplayTag Key =
			FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Save.Cloud")), /*ErrorIfNotFound=*/false);
		return Key;
	}
}
