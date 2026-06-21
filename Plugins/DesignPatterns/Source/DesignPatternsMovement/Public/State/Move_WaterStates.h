// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "State/Move_MovementState.h"
#include "Move_WaterStates.generated.h"

/**
 * Swim: locomotion inside a water volume. On enter it switches the CMC to MOVE_Swimming and applies the
 * profile's swim speed to MaxSwimSpeed. Entry/exit are gated by guards reading UMove_TraceLibrary::IsInWater
 * so the machine drops into and out of swimming as the character crosses a water surface. Free-3D movement
 * is delegated to the CMC's native swimming handling; this state only configures it.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Swim : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Swim();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual bool CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const override;
};
