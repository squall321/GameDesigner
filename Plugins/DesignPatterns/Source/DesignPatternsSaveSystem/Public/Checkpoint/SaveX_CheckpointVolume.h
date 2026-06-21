// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "SaveX_CheckpointVolume.generated.h"

class USaveX_CheckpointComponent;

/**
 * A placed trigger volume that records a checkpoint when an eligible pawn enters it.
 *
 * Authority-only: the overlap handler guards HasAuthority() and only the server records the checkpoint
 * (which writes the reserved checkpoint slot through the wrapped save subsystem). The volume itself holds no
 * replicated state — the checkpoint marker lives on the overlapping actor's USaveX_CheckpointComponent.
 *
 * Eligibility: by default only player-controlled pawns trigger the volume (designers usually do not want AI
 * tripping checkpoints). The volume finds the USaveX_CheckpointComponent on the overlapping actor and calls
 * RecordCheckpoint with this volume's CheckpointId. A volume can be set to fire once or every entry.
 */
UCLASS(Blueprintable, meta = (DisplayName = "DP Checkpoint Volume"))
class DESIGNPATTERNSSAVESYSTEM_API ASaveX_CheckpointVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASaveX_CheckpointVolume();

	//~ Begin AActor
	virtual void BeginPlay() override;
	//~ End AActor

	/** Logical id passed to RecordCheckpoint (e.g. a region/area tag). May be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save|Checkpoint")
	FGameplayTag CheckpointId;

	/** When true, only player-controlled pawns trigger this volume; AI and other actors are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save|Checkpoint")
	bool bPlayerPawnsOnly = true;

	/** When true, the volume records at most once (then disables overlap). When false, every entry records. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Save|Checkpoint")
	bool bOneShot = true;

protected:
	/** Authority overlap handler: validates eligibility and records the checkpoint via the actor's component. */
	UFUNCTION()
	void HandleActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor);

	/** True if OtherActor is allowed to trigger this volume per bPlayerPawnsOnly. */
	bool IsEligibleActor(const AActor* OtherActor) const;

private:
	/** True once this volume has recorded (used to enforce bOneShot). Authority-only bookkeeping. */
	bool bHasFired = false;
};
