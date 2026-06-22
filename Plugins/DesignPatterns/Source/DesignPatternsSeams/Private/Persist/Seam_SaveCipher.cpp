// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Persist/Seam_SaveCipher.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_SaveCipher.
 *
 * These form a fail-OPEN identity cipher: the buffer passes through unchanged and IsEnabled() reports
 * false. The SaveSystem storage subsystem treats "not enabled" as "write plaintext", so a project that
 * registers no cipher (or a subclass that forgets to override a method) always produces a loadable save
 * rather than a corrupt one. A real cipher overrides all three methods.
 */

bool ISeam_SaveCipher::EncryptBuffer_Implementation(const TArray<uint8>& In, FGuid& OutKeyId, TArray<uint8>& Out) const
{
	// Identity passthrough; no key was used.
	OutKeyId = FGuid();
	Out = In;
	return true;
}

bool ISeam_SaveCipher::DecryptBuffer_Implementation(const TArray<uint8>& In, const FGuid& /*KeyId*/, TArray<uint8>& Out) const
{
	// Identity passthrough — matches the identity EncryptBuffer above.
	Out = In;
	return true;
}

bool ISeam_SaveCipher::IsEnabled_Implementation() const
{
	// Disabled by default so the storage pipeline writes plaintext unless a real cipher opts in.
	return false;
}
