// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Shake/Cam_ShakeProfile.h"
#include "Cam_ShakeDirectorComponent.generated.h"

class UCam_ShakeRequestComponent;
class APlayerCameraManager;
class APlayerController;

/**
 * Cosmetic payload broadcast on Cam.Bus.ShakeEpicenter when the advanced director plays a positional
 * shake, so project-side systems can react without coupling. Carried inside an FInstancedStruct on the
 * bus (never plain-replicated).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ShakeEpicenterEvent
{
	GENERATED_BODY()

	/** World-space epicenter of the shake. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Shake")
	FVector Epicenter = FVector::ZeroVector;

	/** The library/profile shake tag that played. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag ShakeTag;

	/** Final scale applied (after falloff + accessibility). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Shake")
	float FinalScale = 0.f;

	FCam_ShakeEpicenterEvent() = default;
};

/**
 * Advanced, data-driven shake layer that sits BESIDE the unchanged UCam_ShakeRequestComponent.
 *
 * It maps event/bus channel tags to UCam_ShakeProfile assets, computes a distance-falloff multiplier
 * from a shake epicenter to the local viewer, layers additive profiles, and multiplies by the cached
 * accessibility ScreenShakeScale (received through ISeam_AccessibilityConsumer). It DELEGATES actual
 * playback to a sibling UCam_ShakeRequestComponent::PlayShakeByTag(ShakeTag, finalScale) when one is
 * present (FindComponentByClass) — so there is one playback path — and falls back to the owning
 * APlayerCameraManager::StartCameraShake only when no request component exists.
 *
 * LOCAL / COSMETIC: never replicated; reacts to already-replicated gameplay re-broadcast on the bus.
 * Early-outs on non-local / server owners. All listeners are removed in EndPlay.
 */
UCLASS(ClassGroup = "DesignPatterns", meta = (BlueprintSpawnableComponent), Blueprintable,
	HideCategories = ("Collision", "Cooking", "AssetUserData", "Replication"))
class DESIGNPATTERNSCAMERA_API UCam_ShakeDirectorComponent : public UActorComponent, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	UCam_ShakeDirectorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISeam_AccessibilityConsumer
	/** Caches ScreenShakeScale so every subsequent shake honours the accessibility "reduce shake" slider. */
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/**
	 * Play the profile mapped to EventTag at a world Epicenter, scaled by distance falloff *
	 * accessibility scale * ExtraScale. Resolves the local viewer location for the falloff.
	 * @return true if a shake was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	bool PlayShakeAtLocation(FGameplayTag EventTag, FVector Epicenter, float ExtraScale = 1.f);

	/** Set the cached accessibility shake scale directly (for projects not driving it via the seam). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	void SetShakeScale(float InScale) { CachedShakeScale = FMath::Max(InScale, 0.f); }

	/** Suppress / un-suppress all advanced shakes (e.g. while photo mode is active). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Shake")
	void SetSuppressed(bool bInSuppressed) { bSuppressed = bInSuppressed; }

protected:
	/** Event/bus channel -> shake profile routes. Drives the bus subscriptions set up at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	TArray<FCam_ShakeProfileRoute> ProfileRoutes;

	/** When true the director broadcasts FCam_ShakeEpicenterEvent on Cam.Bus.ShakeEpicenter on each play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	bool bBroadcastEpicenter = false;

	/** When false every PlayShakeAtLocation is suppressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Shake")
	bool bSuppressed = false;

private:
	/** Resolve the local viewer (camera) world location for the falloff distance, or owner location. */
	FVector ResolveViewerLocation() const;

	/** Resolve the sibling request component (for delegated playback), or null. */
	UCam_ShakeRequestComponent* ResolveRequestComponent() const;

	/** Resolve the owning local player's camera manager (fallback playback path), or null. */
	APlayerCameraManager* ResolveCameraManager() const;

	/** Resolve the controlling local player controller for the owner, or null. */
	APlayerController* ResolveOwningController() const;

	/** Subscribe to the message bus for every authored ProfileRoute (deduplicated by channel). */
	void RegisterBusRoutes();

	/** Handle a bus broadcast: find the route, read an epicenter from the payload, play the profile. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Play a resolved profile at an epicenter with an extra scale (shared core). */
	bool PlayProfile(UCam_ShakeProfile* Profile, FGameplayTag Channel, FVector Epicenter, float ExtraScale);

	/** Cached accessibility ScreenShakeScale (1 = full). Updated via the consumer seam. */
	UPROPERTY(Transient)
	float CachedShakeScale = 1.f;

	/** Listener handles for our bus subscriptions, removed in EndPlay. */
	TArray<FDP_ListenerHandle> ListenerHandles;
};
