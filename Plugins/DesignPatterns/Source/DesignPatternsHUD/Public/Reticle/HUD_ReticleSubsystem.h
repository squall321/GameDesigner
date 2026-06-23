// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "MessageBus/DPMessage.h"
#include "Identity/Seam_TeamAffinity.h"
#include "HUD_ReticleSubsystem.generated.h"

class UHUD_ReticleViewModel;
class UHUD_ReticleConfigDataAsset;
class APlayerController;

/**
 * Local-player-scoped crosshair/reticle controller.
 *
 * Owns a UHUD_ReticleViewModel. Listens on the combat hit-feedback channel (DP.Bus.Combat.HitFeedback) and,
 * when the local pawn is the instigator (read by reflection), pulses the hit-confirm marker (stronger for
 * crits). Reads the local viewer's weapon spread from a configurable bus channel (reflected float field).
 * Each tick (FTSTicker, gated on a valid PC + viewport) it traces forward from the camera and maps
 * ISeam_TeamAffinity::AreFriendly(localPawn, target) to a friendly / hostile / neutral target-type tag for
 * team-colored reticles.
 *
 * Purely local/cosmetic: never replicates, never mutates gameplay; combat data arrives only via the bus
 * (read by reflection) and team relation via the seam — no Combat / GameMode header is included.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_ReticleSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** The ViewModel the reticle UI binds to (never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Reticle")
	UHUD_ReticleViewModel* GetViewModel() const { return ViewModel; }

	/** Replace the config/tuning asset. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Reticle")
	void SetConfig(UHUD_ReticleConfigDataAsset* InConfig);

private:
	/** Hit-feedback handler: if the local pawn is the instigator, trigger a hit-confirm pulse. */
	void HandleHitFeedback(const FDP_Message& Message);

	/** Spread handler: read the reflected spread float off the payload and push it to the VM. */
	void HandleSpread(const FDP_Message& Message);

	/** Per-frame: decay hit-confirm, trace forward, resolve + push the centre target-type tag. */
	bool TickReticle(float DeltaTime);

	/** Trace forward from the camera and resolve the centre target's team relation to a target-type tag. */
	FGameplayTag ResolveTargetType(AActor* LocalPawn, APlayerController* PC) const;

	/** Resolve the world's ISeam_TeamAffinity provider (by service tag), held weakly; null when absent. */
	void ResolveTeamAffinity();

	/** The owning local player's current PlayerController (null if not yet possessed). */
	APlayerController* GetOwningPlayerController() const;

	// --- Reflection payload readers (shared shape with the damage-number subsystem; no Combat header) ---
	static AActor* ReadActorField(const FInstancedStruct& Payload, FName FieldName);
	static bool ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue);
	static bool ReadTagField(const FInstancedStruct& Payload, FName FieldName, FGameplayTag& OutValue);

	/** The pure-projection ViewModel (owned, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_ReticleViewModel> ViewModel = nullptr;

	/** The config/tuning asset (owned ref, GC-kept while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_ReticleConfigDataAsset> Config = nullptr;

	/** The world team-affinity provider, held weakly (re-resolved when stale). */
	TWeakInterfacePtr<ISeam_TeamAffinity> TeamAffinity;

	/** Time (world seconds) the last hit-confirm was triggered, and its peak strength. */
	double LastHitConfirmTime = -1000.0;
	float HitConfirmPeak = 0.f;

	/** FTSTicker handle driving TickReticle; removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;
};
