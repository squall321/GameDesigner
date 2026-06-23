// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "MessageBus/DPMessage.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "HUD_ScreenStateEffectSubsystem.generated.h"

class UHUD_ScreenStateViewModel;
class UHUD_ScreenStateConfigDataAsset;
class ISeam_VfxController;
class APlayerController;

/**
 * Local-player-scoped full-screen state-effect controller (low-health vignette + hit-direction indicators
 * + damage flash).
 *
 * Drives a UHUD_ScreenStateViewModel ONLY (the bound widget applies MPC/material params — this subsystem
 * never touches materials). The low-health vignette intensity comes from a configurable health-fraction bus
 * channel (reflected float) or SetHealthFraction. Hit-direction bearings are computed from the combat
 * hit-feedback channel (reflected ImpactPoint/Instigator vs the camera forward). It implements
 * ISeam_AccessibilityConsumer to honor reduce-flashing (ScreenShakeScale is reused as a flash scale), and
 * may optionally route a one-shot flash VFX through ISeam_VfxController.
 *
 * Purely local/cosmetic: never replicates, never mutates gameplay; combat/health data arrives only via the
 * bus (read by reflection) — no Combat header is included.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_ScreenStateEffectSubsystem : public ULocalPlayerSubsystem, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AccessibilityConsumer
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/** The ViewModel the screen-state widget binds to (never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|ScreenState")
	UHUD_ScreenStateViewModel* GetViewModel() const { return ViewModel; }

	/** Replace the config/tuning asset. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|ScreenState")
	void SetConfig(UHUD_ScreenStateConfigDataAsset* InConfig);

	/** Directly set the local viewer's health fraction in [0,1] (drives the low-health vignette). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|ScreenState")
	void SetHealthFraction(float Frac);

private:
	/** One active hit-direction indicator (world bearing source + spawn time). */
	struct FActiveHitDir
	{
		float AngleDegrees = 0.f;
		double SpawnTimeSeconds = 0.0;
	};

	/** Health-fraction bus handler: read the reflected float and update the vignette source. */
	void HandleHealthFraction(const FDP_Message& Message);

	/** Hit-feedback bus handler: compute a hit-direction bearing + trigger the damage flash (+ optional VFX). */
	void HandleHitFeedback(const FDP_Message& Message);

	/** Per-frame: recompute vignette from health, decay flash + hit-direction indicators, push to the VM. */
	bool TickEffects(float DeltaTime);

	/** Resolve the optional world VFX controller seam (by service tag), held weakly; null when absent. */
	void ResolveVfxController();

	/** The owning local player's current PlayerController (null if not yet possessed). */
	APlayerController* GetOwningPlayerController() const;

	// --- Reflection payload readers (no Combat header) ---
	static AActor* ReadActorField(const FInstancedStruct& Payload, FName FieldName);
	static bool ReadFloatField(const FInstancedStruct& Payload, FName FieldName, float& OutValue);
	static bool ReadVectorField(const FInstancedStruct& Payload, FName FieldName, FVector& OutValue);

	/** The pure-projection ViewModel (owned, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_ScreenStateViewModel> ViewModel = nullptr;

	/** The config/tuning asset (owned ref, GC-kept while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_ScreenStateConfigDataAsset> Config = nullptr;

	/** Optional world VFX controller (held weakly, re-resolved when stale). */
	TWeakInterfacePtr<ISeam_VfxController> VfxController;

	/** Active hit-direction indicators. */
	TArray<FActiveHitDir> HitDirections;

	/** The latest health fraction in [0,1] (drives the vignette). */
	float HealthFraction = 1.f;

	/** Time the last damage flash was triggered (world seconds) and its peak alpha. */
	double LastFlashTime = -1000.0;
	float FlashPeak = 0.f;

	/**
	 * Flash/flash-VFX scale from accessibility options (1 = full, 0 = disabled). Reuses ScreenShakeScale as
	 * the reduce-flashing control so a single accessibility slider damps both shake and the full-screen flash.
	 */
	float FlashScale = 1.f;

	/** FTSTicker handle driving TickEffects; removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;
};
