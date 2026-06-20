// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_InputModeArbiter.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_InputModeArbiter : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared input-mode arbitration seam. Resolves the overlap where several systems independently need to
 * push/pop a UI/game input mode: a Narrative cutscene lock, a HUD menu stack, a Camera photo-mode.
 *
 * This is a thin, priority-keyed push/pop facade that WRAPS the engine's APlayerController::SetInputMode
 * / UI input-config stack — it never reinvents input handling. The concrete implementation is owned by the
 * Platform module (its input router subsystem) and registered into the service locator; consumers resolve
 * a TScriptInterface<ISeam_InputModeArbiter> and never depend on the Platform module's concrete type.
 *
 * The highest-priority active push wins; popping a request restores the next-highest. Pushes are keyed by
 * an opaque request id so a caller can pop exactly its own request without disturbing others.
 */
class DESIGNPATTERNSSEAMS_API ISeam_InputModeArbiter
{
	GENERATED_BODY()

public:
	/**
	 * Request that ModeTag becomes the active input mode at the given priority.
	 * @return an opaque request id; pass it to PopInputMode to release this request.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Input")
	FGuid PushInputMode(FGameplayTag ModeTag, int32 Priority);

	/** Release a previously-pushed request. Safe to call with an invalid/already-popped id (no-op). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Input")
	void PopInputMode(FGuid RequestId);

	/** The currently-winning input mode tag (empty if nothing is pushed). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Input")
	FGameplayTag GetActiveInputMode() const;
};
