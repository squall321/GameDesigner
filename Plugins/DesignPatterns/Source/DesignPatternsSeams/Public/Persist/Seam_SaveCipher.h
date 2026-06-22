// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_SaveCipher.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_SaveCipher : public UInterface
{
	GENERATED_BODY()
};

/**
 * Pluggable byte-buffer encryption seam applied INSIDE the SaveSystem write/read pipeline.
 *
 * The SaveSystem's storage subsystem holds one implementation of this seam as a TScriptInterface and
 * calls EncryptBuffer immediately before the container is written to disk and DecryptBuffer immediately
 * after the container is read back (both off the game thread, on plain byte copies). The project (or a
 * platform module) supplies the concrete cipher — AES, a platform keystore wrapper, etc. — so the
 * framework never depends on a specific crypto SDK.
 *
 * IDENTITY / NO-OP CONTRACT: when no cipher is registered (or IsEnabled() returns false), the storage
 * subsystem skips encryption entirely and writes plaintext. The default implementations below are a
 * fail-OPEN identity transform (Out = In, IsEnabled() = false) so that:
 *   - a save written without a cipher always remains loadable, and
 *   - a Blueprint subclass that forgets to override one method degrades to a readable passthrough
 *     rather than silently corrupting saves.
 * A real cipher MUST override all three methods and return a STABLE OutKeyId so DecryptBuffer can later
 * select the matching key. The key itself is never carried across this seam — only its FGuid identifier
 * is stored in the on-disk container header.
 *
 * THREADING: EncryptBuffer/DecryptBuffer are invoked on a background task with plain (non-UObject) byte
 * arrays; implementations must be thread-safe and must not touch UObjects or the game world. IsEnabled()
 * is queried on the game thread before the pipeline branches.
 */
class DESIGNPATTERNSSEAMS_API ISeam_SaveCipher
{
	GENERATED_BODY()

public:
	/**
	 * Transform In -> Out for writing. Return true on success and set OutKeyId to a stable identifier for
	 * the key used (so DecryptBuffer can later look it up). Returning false aborts the encrypted write and
	 * the storage subsystem falls back to a plaintext container (logged), never losing the save.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	bool EncryptBuffer(const TArray<uint8>& In, FGuid& OutKeyId, TArray<uint8>& Out) const;

	/**
	 * Reverse of EncryptBuffer for reading. KeyId is the value the matching EncryptBuffer returned (read
	 * from the container header). Return true on success; false marks the slot unreadable so the storage
	 * subsystem can attempt backup recovery.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	bool DecryptBuffer(const TArray<uint8>& In, const FGuid& KeyId, TArray<uint8>& Out) const;

	/**
	 * True if this cipher should actually transform buffers. A registered-but-disabled cipher (e.g. while
	 * a key is unavailable) makes the storage subsystem write plaintext rather than risk an unreadable save.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Persist")
	bool IsEnabled() const;
};
