// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Vfx/WS_VfxCarrier.h"

#include "Core/DPLog.h"
#include "Components/SceneComponent.h"
#include "FXSystemAsset.h"               // UFXSystemAsset (Engine module).
#include "Particles/FXSystemComponent.h" // UFXSystemComponent + OnSystemFinished (Engine module).
#include "Kismet/GameplayStatics.h"
#include "Particles/WorldPSCPool.h"      // EPSCPoolMethod for the SpawnEmitterAttached overload.

AWS_VfxCarrier::AWS_VfxCarrier()
{
	PrimaryActorTick.bCanEverTick = false;

	// Purely cosmetic, never replicated.
	bReplicates = false;
	SetReplicatingMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// FX components are created on demand in ActivateSystem (we cannot know the backend at construction).
}

bool AWS_VfxCarrier::ActivateSystem(UFXSystemAsset* System, float UniformScale, bool bAutoActivate)
{
	if (!System)
	{
		return false;
	}

	// Tear down any prior effect so this carrier hosts exactly one system at a time.
	DeactivateSystem();

	const float Scale = FMath::Max(0.01f, UniformScale);

	// UGameplayStatics::SpawnEmitterAttached accepts the UFXSystemAsset base and transparently routes to
	// Cascade or Niagara, creating and returning the correct UFXSystemComponent. This is the engine-blessed
	// spawn path that needs only the Engine module (no Niagara dependency). Attached to our root so moving
	// the carrier moves the effect.
	UFXSystemComponent* Spawned = UGameplayStatics::SpawnEmitterAttached(
		System,
		SceneRoot,
		NAME_None,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		FVector(Scale),
		EAttachLocation::SnapToTargetIncludingScale,
		/*bAutoDestroy*/ false, // the pool owns lifetime, not auto-destroy.
		bAutoActivate ? EPSCPoolMethod::None : EPSCPoolMethod::None,
		bAutoActivate);

	if (!Spawned)
	{
		// No RHI / null system -> inert. Caller treats this as a no-op cosmetic spawn.
		return false;
	}

	FxComponent = Spawned;
	bEffectActive = true;

	// Bind completion so a non-looping system recycles the carrier when it finishes.
	FxComponent->OnSystemFinished.AddDynamic(this, &AWS_VfxCarrier::HandleSystemFinished);

	return true;
}

void AWS_VfxCarrier::DeactivateSystem()
{
	bEffectActive = false;

	if (FxComponent)
	{
		FxComponent->OnSystemFinished.RemoveDynamic(this, &AWS_VfxCarrier::HandleSystemFinished);
		FxComponent->Deactivate();
		FxComponent->DestroyComponent();
		FxComponent = nullptr;
	}
}

bool AWS_VfxCarrier::IsEffectActive() const
{
	return bEffectActive && FxComponent != nullptr && FxComponent->IsActive();
}

void AWS_VfxCarrier::HandleSystemFinished(UFXSystemComponent* FinishedComponent)
{
	// A non-looping effect completed. Mark inactive and tell the manager to recycle us.
	bEffectActive = false;
	OnEffectFinished.Broadcast(this);
}

// =====================================================================================================
// IDP_Poolable
// =====================================================================================================

void AWS_VfxCarrier::OnAcquiredFromPool_Implementation()
{
	// Fresh out of the pool: no effect yet. ActivateSystem (called by the manager) arms it.
	bEffectActive = false;
}

void AWS_VfxCarrier::OnReturnedToPool_Implementation()
{
	// Mirror of acquire: tear down the hosted effect so the next acquirer gets a clean carrier (the pool
	// itself does not reset latent particle state — that is this hook's job per the pool contract).
	DeactivateSystem();
	OnEffectFinished.Clear();
}

bool AWS_VfxCarrier::CanBeReclaimed_Implementation() const
{
	// Do not let an idle-eviction sweep steal a carrier mid-burst.
	return !IsEffectActive();
}
