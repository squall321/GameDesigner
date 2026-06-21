// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FSM/DPStateMachineDefinition.h"
#include "Move_MovementStateMachineDefinition.generated.h"

/**
 * A movement-specialized FSM definition asset. It is functionally a UDP_StateMachineDefinition (which is
 * a UPrimaryDataAsset) whose authored States are UMove_MovementState subclasses and whose InitialStateTag
 * defaults to Move.State.Walk. The subclass exists only to:
 *   - set the locomotion-appropriate InitialStateTag default, and
 *   - run movement-specific editor validation (every authored state must be a UMove_MovementState, the
 *     initial state must resolve, and the canonical Walk state should be present).
 *
 * Multiple movement components share ONE of these (the FSM "definition" pattern): the graph is data,
 * authored once, with no per-instance allocation.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSMOVEMENT_API UMove_MovementStateMachineDefinition : public UDP_StateMachineDefinition
{
	GENERATED_BODY()

public:
	UMove_MovementStateMachineDefinition();

#if WITH_EDITOR
	/** Movement-specific validation layered on top of the base graph validation. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
