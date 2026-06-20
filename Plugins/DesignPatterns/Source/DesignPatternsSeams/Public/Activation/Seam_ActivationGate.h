// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ActivationGate.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ActivationGate : public UInterface
{
	GENERATED_BODY()
};

/**
 * A tiny activation-gate seam. Lets systems gate content (encounter regions, procedural placement,
 * streaming) on game-state conditions WITHOUT depending on the World hub's concrete types — a small
 * World-side adapter decodes IWorldHub_Queryable / FWorldHub_Scope internally and answers this seam.
 *
 * Consumers (e.g. LevelDirector) resolve a TScriptInterface<ISeam_ActivationGate> from the service
 * locator; when unresolved the documented default is "open" (active), so a project without the World
 * hub still runs.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ActivationGate
{
	GENERATED_BODY()

public:
	/** True if the gate identified by GateKey is currently open (the gated content should be active). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Activation")
	bool IsGateOpen(FGameplayTag GateKey) const;
};
