// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

// Engine AIModule types — PRIVATE dependency. This is a PRIVATE header (never shipped in Public/)
// so including AIModule here does not leak it to consumers of the module's public API.
#include "Perception/AIPerceptionTypes.h"

#include "AI_PerceptionListenerProxy.generated.h"

class UAI_PerceptionComponent;
class AActor;

/**
 * Internal proxy that owns the UFUNCTION bound to the engine's dynamic perception-updated delegate
 * and forwards normalized parameters back to its owning UAI_PerceptionComponent.
 *
 * It exists ONLY so the engine struct FAIStimulus never appears in the module's PUBLIC headers
 * (which would force AIModule onto every consumer). Lives in a private header so UHT generates its
 * reflection body while AIModule stays a private module dependency.
 */
UCLASS()
class UAI_PerceptionListenerProxy : public UObject
{
	GENERATED_BODY()

public:
	/** The adapter that owns this proxy; weak so a destroyed adapter cannot keep it alive. */
	TWeakObjectPtr<UAI_PerceptionComponent> Owner;

	/** Bound to UAIPerceptionComponent::OnTargetPerceptionUpdated; classifies + forwards the stimulus. */
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
};
