// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Pool/DPPoolable.h"
#include "WS_VfxCarrier.generated.h"

class UFXSystemAsset;
class UFXSystemComponent;
class USceneComponent;

/**
 * Lightweight, poolable carrier actor that hosts ONE cosmetic particle effect.
 *
 * The VFX manager recycles these through the core object pool: each acquire activates the carrier's FX
 * component with a resolved UFXSystemAsset (the engine base of both Cascade and Niagara), each release
 * deactivates and clears it. Using the engine UFXSystemComponent / UFXSystemAsset base types and the
 * engine spawn helpers means this carrier compiles and works without a hard Niagara module dependency.
 *
 * The carrier is purely LOCAL/COSMETIC and never replicates. It implements IDP_Poolable so the pool's
 * reset contract is honoured (the FX component's latent particle state is torn down on return).
 */
UCLASS(NotPlaceable)
class DESIGNPATTERNSWORLDSYSTEMS_API AWS_VfxCarrier : public AActor, public IDP_Poolable
{
	GENERATED_BODY()

public:
	AWS_VfxCarrier();

	/**
	 * Activate this carrier with System, playing it at the actor's current transform (use SetActorTransform
	 * / attachment before calling). UniformScale scales the effect. Returns false (inert) if System is null
	 * or the FX component could not be created (e.g. no RHI).
	 * @param System       The Cascade/Niagara system to play (engine base type).
	 * @param UniformScale Cosmetic scale applied to the FX component.
	 * @param bAutoActivate Whether to begin playing immediately.
	 */
	bool ActivateSystem(UFXSystemAsset* System, float UniformScale, bool bAutoActivate = true);

	/** Deactivate and clear the hosted effect (detaches if attached, stops the component). */
	void DeactivateSystem();

	/** The current FX component, or null if none is active. Non-owning accessor. */
	UFXSystemComponent* GetFxComponent() const { return FxComponent; }

	/** True if the hosted effect is currently active (playing or fading). */
	bool IsEffectActive() const;

	/**
	 * Bound to the FX component's finished delegate for non-looping systems. Notifies the manager (via the
	 * OnEffectFinished delegate) so it can recycle this carrier. Bound/unbound per activation.
	 */
	UFUNCTION()
	void HandleSystemFinished(UFXSystemComponent* FinishedComponent);

	/** Fired when a non-looping hosted effect reports completion. The manager listens to recycle the carrier. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FWS_OnCarrierEffectFinished, AWS_VfxCarrier* /*Carrier*/);
	FWS_OnCarrierEffectFinished OnEffectFinished;

	//~ Begin IDP_Poolable
	/** On acquire there is nothing to re-arm until ActivateSystem is called; reset the finished flag. */
	virtual void OnAcquiredFromPool_Implementation() override;
	/** On return tear down the hosted effect so the next acquirer gets a clean carrier. */
	virtual void OnReturnedToPool_Implementation() override;
	/** Allow reclamation only once the hosted effect is no longer active (so a mid-burst is not stolen). */
	virtual bool CanBeReclaimed_Implementation() const override;
	//~ End IDP_Poolable

private:
	/** Scene root so the carrier can be placed/attached and the FX component parented under it. */
	UPROPERTY(VisibleAnywhere, Category = "Vfx")
	TObjectPtr<USceneComponent> SceneRoot;

	/**
	 * The hosted FX component, created lazily on first ActivateSystem to match the system's backend
	 * (Niagara when available, otherwise Cascade). Held as a UPROPERTY so it stays GC-rooted while live.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UFXSystemComponent> FxComponent;

	/** True while a non-looping effect is still playing (drives CanBeReclaimed). */
	bool bEffectActive = false;
};
