// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/HUD_Trackable.h"
#include "HUD_MarkerComponent.generated.h"

class UHUD_MarkerRegistrySubsystem;

/**
 * Drop-in component that makes its owning actor appear on the minimap / world-marker layer.
 *
 * Implements IHUD_Trackable and self-registers into the world UHUD_MarkerRegistrySubsystem on BeginPlay,
 * unregistering on EndPlay. The minimap projection never sees this component's concrete type — it only
 * ever talks to the IHUD_Trackable seam. All marker behaviour is data-driven via EditAnywhere tunables
 * (no magic constants): which icon tag to show, whether to track, and an optional vertical offset applied
 * to the owner's location so e.g. a character's blip can be taken from the head/feet rather than origin.
 *
 * Purely cosmetic and never replicated: the owning actor is already replicated, so the component exists on
 * every machine and registers locally. Visibility/marker-tag changes are driven by gameplay through the
 * setters (which can be called from already-replicated OnRep paths) and re-broadcast the registry's
 * change delegate so any live ViewModel refreshes.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Cooking, Activation, ComponentTick))
class DESIGNPATTERNSHUD_API UHUD_MarkerComponent : public UActorComponent, public IHUD_Trackable
{
	GENERATED_BODY()

public:
	UHUD_MarkerComponent();

	//~ Begin IHUD_Trackable
	virtual FVector GetWorldLocation_Implementation() const override;
	virtual FGameplayTag GetMarkerTag_Implementation() const override;
	virtual bool IsVisibleOnMap_Implementation() const override;
	//~ End IHUD_Trackable

	/**
	 * Set the icon-selecting marker kind tag at runtime (e.g. switch an NPC from neutral to hostile).
	 * Broadcasts the registry change so live ViewModels re-read.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetMarkerTag(FGameplayTag InMarkerTag);

	/** The current icon-selecting marker kind tag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	FGameplayTag GetConfiguredMarkerTag() const { return MarkerTag; }

	/**
	 * Toggle per-frame map visibility (e.g. hide while stealthed / undiscovered). Registration is kept;
	 * this only flips the IsVisibleOnMap gate. Broadcasts the registry change.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetVisibleOnMap(bool bInVisible);

	/** Whether this marker currently reports itself visible to the map. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	bool IsConfiguredVisibleOnMap() const { return bVisibleOnMap; }

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Icon-selecting marker kind (e.g. DP.HUD.Marker.Enemy). Designer-assigned; defaults to the generic
	 * point-of-interest kind so an unconfigured marker still shows with the fallback icon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	FGameplayTag MarkerTag;

	/** Whether the marker starts visible on the map. Runtime toggled via SetVisibleOnMap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	bool bVisibleOnMap = true;

	/**
	 * Vertical offset (cm) added to the owner's actor location when reporting GetWorldLocation. Lets the
	 * tracked point be lifted/dropped from the actor origin without a magic constant baked in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	float WorldLocationZOffset = 0.f;

private:
	/** Resolve the world marker registry for this component's world (null in editor/preview). */
	UHUD_MarkerRegistrySubsystem* ResolveRegistry() const;

	/** Re-broadcast the registry's change delegate so live ViewModels refresh after a runtime mutation. */
	void NotifyRegistryChanged();

	/** Cached registry resolved at BeginPlay so EndPlay unregisters from the same instance. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UHUD_MarkerRegistrySubsystem> RegistryWeak;

	/** True between successful register and unregister — guards double register/unregister. */
	bool bRegistered = false;
};
