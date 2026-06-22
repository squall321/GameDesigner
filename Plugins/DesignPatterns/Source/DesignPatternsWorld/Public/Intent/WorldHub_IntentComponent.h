// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "WorldHub_IntentComponent.generated.h"

class UWorldHub_FactionMatrixComponent;
class UWorldHub_HistorySubsystem;

/**
 * CLIENT -> SERVER INTENT carrier for world-hub authoritative operations.
 *
 * Placed on a PLAYER-OWNED actor (PlayerController / Pawn) so its Server RPCs are routed from the
 * owning client to the server. This is the ONLY client-side path to request a faction-standing change
 * or a checkpoint rewind: the validated Server RPCs resolve the SERVER-side authority components /
 * subsystems and call their Authority_* / rewind API, which RE-VALIDATES the request. Clients never
 * touch the rep carrier or the authority components directly.
 *
 * The component carries no replicated state of its own; it is a pure intent funnel.
 */
UCLASS(ClassGroup = (DesignPatternsWorld), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSWORLD_API UWorldHub_IntentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldHub_IntentComponent();

	/**
	 * Request that faction A's standing toward B be adjusted by Delta (client -> server). Server-only
	 * execution re-resolves the matrix component and re-validates / re-clamps before applying.
	 */
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "DesignPatterns|WorldHub|Intent")
	void Server_RequestStandingChange(FGameplayTag A, FGameplayTag B, float Delta);

	/**
	 * Request a rewind of world-hub state to a named checkpoint (client -> server). Server-only
	 * execution resolves the history subsystem, which applies the rewind under its own authority gate.
	 */
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "DesignPatterns|WorldHub|Intent")
	void Server_RequestRewindToCheckpoint(FGameplayTag CheckpointLabel);

	/**
	 * Optional EditAnywhere safety cap on the magnitude of a single standing-change request, so a
	 * compromised client cannot swing standings arbitrarily in one call. 0 disables the cap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"), Category = "DesignPatterns|WorldHub|Intent")
	float MaxStandingDeltaPerRequest = 0.0f;

private:
	/** Resolve the SERVER-side faction matrix component (searches the GameState / a registered carrier). */
	UWorldHub_FactionMatrixComponent* ResolveFactionMatrix() const;
};
