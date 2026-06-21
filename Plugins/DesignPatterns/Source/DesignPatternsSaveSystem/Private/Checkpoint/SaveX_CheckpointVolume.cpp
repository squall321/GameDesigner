// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Checkpoint/SaveX_CheckpointVolume.h"

#include "Checkpoint/SaveX_CheckpointComponent.h"

#include "Core/DPLog.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Components/BrushComponent.h"

ASaveX_CheckpointVolume::ASaveX_CheckpointVolume()
{
	// The volume is a passive trigger; it needs to exist on the server (authority records the checkpoint).
	// AVolume already replicates its existence by default for placed actors; we do not add replicated state.
	bReplicates = true;
	SetReplicatingMovement(false);

	// Make the brush a query-only overlap trigger so pawns generate overlap events without blocking movement.
	if (UBrushComponent* Brush = GetBrushComponent())
	{
		Brush->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Brush->SetCollisionResponseToAllChannels(ECR_Overlap);
		Brush->SetGenerateOverlapEvents(true);
	}
}

void ASaveX_CheckpointVolume::BeginPlay()
{
	Super::BeginPlay();

	// Only the authority should react to overlaps (it owns the save write). Binding on clients would be
	// harmless because the handler also guards authority, but we avoid the wasted delegate entirely.
	if (HasAuthority())
	{
		OnActorBeginOverlap.AddDynamic(this, &ASaveX_CheckpointVolume::HandleActorBeginOverlap);
	}
}

void ASaveX_CheckpointVolume::HandleActorBeginOverlap(AActor* /*OverlappedActor*/, AActor* OtherActor)
{
	// AUTHORITY GUARD: recording mutates persisted + replicated checkpoint state.
	if (!HasAuthority())
	{
		return;
	}
	if (bOneShot && bHasFired)
	{
		return;
	}
	if (!IsEligibleActor(OtherActor))
	{
		return;
	}

	// Find the checkpoint component on the overlapping actor (or its controller's pawn chain). The component
	// owns the actual record + slot write; the volume is purely the spatial trigger.
	USaveX_CheckpointComponent* Component = OtherActor->FindComponentByClass<USaveX_CheckpointComponent>();
	if (!Component)
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[CheckpointVolume] '%s' overlapped by '%s' which has no checkpoint component."),
			*GetName(), *GetNameSafe(OtherActor));
		return;
	}

	const bool bRecorded = Component->RecordCheckpoint(CheckpointId);
	if (bRecorded)
	{
		bHasFired = true;
		if (bOneShot)
		{
			// Stop reacting to further overlaps for a one-shot checkpoint.
			OnActorBeginOverlap.RemoveDynamic(this, &ASaveX_CheckpointVolume::HandleActorBeginOverlap);
		}
		UE_LOG(LogDPSave, Log, TEXT("[CheckpointVolume] '%s' recorded checkpoint '%s' for '%s'."),
			*GetName(), *CheckpointId.ToString(), *GetNameSafe(OtherActor));
	}
}

bool ASaveX_CheckpointVolume::IsEligibleActor(const AActor* OtherActor) const
{
	if (!OtherActor)
	{
		return false;
	}
	if (!bPlayerPawnsOnly)
	{
		return true;
	}

	// Player-only: the overlapping actor must be a pawn possessed by a player controller.
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn)
	{
		return false;
	}
	const AController* Controller = Pawn->GetController();
	return Controller != nullptr && Controller->IsPlayerController();
}
