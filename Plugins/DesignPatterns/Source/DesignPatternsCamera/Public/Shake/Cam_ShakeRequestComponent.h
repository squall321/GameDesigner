// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Cam_ShakeRequestComponent.generated.h"

class UCam_CameraShakeLibrary;
class APlayerCameraManager;
class APlayerController;

/**
 * One designer-authored route: "when this bus channel fires, play this shake (tag) at this scale".
 *
 * The Channel is matched hierarchy-aware against broadcasts (a route on DP.Bus.Combat.Damaged fires
 * for DP.Bus.Combat.Damaged.Critical too). ShakeTag, when set, names the library row to play;
 * when left empty the component falls back to looking the broadcast Channel itself up in the library
 * (so authors can key library rows directly by bus channel and skip routes entirely).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ShakeBusRoute
{
	GENERATED_BODY()

	/** Bus channel to subscribe to (under DP.Bus). Matched hierarchy-aware. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag Channel;

	/** Library row to play. If invalid, the broadcast Channel is used as the library lookup key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag ShakeTag;

	/** Extra per-route scale multiplied on top of the library entry's DefaultScale. Clamped >= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RouteScale = 1.f;

	FCam_ShakeBusRoute() = default;
};

/**
 * Player-owned component that turns gameplay events into camera shakes — a clean tag/data API
 * over APlayerCameraManager::StartCameraShake.
 *
 * RESPONSIBILITIES
 *  - PlayShakeByTag(Tag, Scale): look the tag up in the shake library, instantiate the mapped
 *    UCameraShakeBase via the owning player's camera manager, scaled by Library.DefaultScale * Scale.
 *  - Bus listening: for each authored FCam_ShakeBusRoute, subscribe to the message bus and play the
 *    mapped shake when that channel fires. This is how combat hits become screen shake WITHOUT any
 *    Combat include — the coupling is purely the DP.Bus.* tag + the data-driven library.
 *
 * COSMETIC / LOCAL ONLY: shake is presentation. This component is NOT replicated; it reacts to
 * already-replicated gameplay re-broadcast locally on the bus. Attach it to the locally-controlled
 * player pawn or player controller. It no-ops on dedicated servers (no camera manager / not local).
 *
 * The shake library is resolved from UCam_DeveloperSettings (project CDO) unless an explicit override is set.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentTick", "Collision", "Cooking", "AssetUserData", "Replication"))
class DESIGNPATTERNSCAMERA_API UCam_ShakeRequestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCam_ShakeRequestComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Play the shake mapped to ShakeTag in the active library, scaled by RequestScale.
	 * Effective scale handed to StartCameraShake = LibraryEntry.DefaultScale * RequestScale.
	 * No-op (logged at Verbose) when not locally controlled, when no library/entry/class resolves,
	 * or on a server with no camera manager.
	 * @return true if a shake instance was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	bool PlayShakeByTag(FGameplayTag ShakeTag, float RequestScale = 1.f);

	/**
	 * Stop a specific shake class on the owning camera manager (e.g. cancel a looping shake), or all
	 * shakes if ShakeClass is null. Thin pass-through to APlayerCameraManager::StopAllInstancesOfCameraShake
	 * / StopAllCameraShakes. Cosmetic; safe to call when nothing is playing.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	void StopShakes(TSubclassOf<UCameraShakeBase> ShakeClass, bool bImmediately = false);

	/** Set the active shake library at runtime (overrides the settings-resolved default). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	void SetShakeLibrary(UCam_CameraShakeLibrary* InLibrary);

	/** @return the library this component is currently using (override, else settings default). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Shake")
	UCam_CameraShakeLibrary* GetActiveShakeLibrary() const;

protected:
	/**
	 * Optional explicit library override. When null the component resolves the project default
	 * from UCam_DeveloperSettings at BeginPlay. Soft so the override does not force-load content for
	 * actors that never need it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	TSoftObjectPtr<UCam_CameraShakeLibrary> ShakeLibraryOverride;

	/**
	 * Bus-channel -> shake routes. Drives the bus subscriptions set up at BeginPlay. Authored per
	 * project (or left empty to rely solely on library rows keyed directly by bus channel + explicit
	 * PlayShakeByTag calls).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	TArray<FCam_ShakeBusRoute> BusRoutes;

	/**
	 * Global multiplier applied to every shake this component plays (accessibility / "reduce shake").
	 * Defaults to a defensive 1.0; projects expose this to a settings slider. Clamped >= 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float GlobalShakeScale = 1.f;

	/** When false, every PlayShakeByTag is suppressed (e.g. photo mode / accessibility off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	bool bShakesEnabled = true;

private:
	/** The library currently in use (resolved override or settings default). Non-owning content ref. */
	UPROPERTY(Transient)
	TObjectPtr<UCam_CameraShakeLibrary> ActiveLibrary;

	/**
	 * Resolve the owning local player's camera manager, or null if this actor is not locally
	 * controlled / has no player controller (e.g. on a dedicated server). Never keeps it alive.
	 */
	APlayerCameraManager* ResolveCameraManager() const;

	/** Resolve the controlling APlayerController for the owner (pawn or controller owner), or null. */
	APlayerController* ResolveOwningPlayerController() const;

	/** Resolve & cache ActiveLibrary from override, then settings. Logs once if nothing resolves. */
	void ResolveActiveLibrary();

	/** Subscribe to the message bus for every authored BusRoute (deduplicated by channel). */
	void RegisterBusRoutes();

	/** Handle a bus broadcast for a subscribed channel: find the route and play the mapped shake. */
	void HandleBusMessage(const FDP_Message& Message);
};
