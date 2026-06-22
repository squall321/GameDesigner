// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/HUD_Trackable.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_WorldPromptComponent.generated.h"

class UHUD_MarkerRegistrySubsystem;

/**
 * World-space interaction prompt/marker component placed on the INTERACTABLE actor.
 *
 * Implements IHUD_Trackable and self-registers into the world UHUD_MarkerRegistrySubsystem on
 * BeginPlay (unregistering on EndPlay), exactly like the shipped UHUD_MarkerComponent — so the
 * HUD/minimap layer renders the prompt purely through the trackable seam, with zero coupling between
 * Interaction and HUD beyond that seam.
 *
 * Focus is pushed DOWN to this component by the LOCAL focus owner via SetFocusedLocally (the
 * interactor/cursor/cycler on the controlling client), rather than the marker reaching across actors
 * to find who is looking at it. This is correct on a dedicated server / split-screen: each local
 * viewer decides which prompts are focused for it. Cosmetic, never replicated.
 */
UCLASS(ClassGroup = (DesignPatternsInteraction), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Cooking, Activation, ComponentTick))
class DESIGNPATTERNSINTERACTION_API UInteract_WorldPromptComponent : public UActorComponent, public IHUD_Trackable
{
	GENERATED_BODY()

public:
	UInteract_WorldPromptComponent();

	//~ Begin IHUD_Trackable
	virtual FVector GetWorldLocation_Implementation() const override;
	virtual FGameplayTag GetMarkerTag_Implementation() const override;
	virtual bool IsVisibleOnMap_Implementation() const override;
	//~ End IHUD_Trackable

	/**
	 * Push the local focus state for this prompt (called by the focus owner on the controlling client).
	 * Avail carries the focused verb's availability so the marker can swap to a "locked"/reason style.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact|WorldPrompt")
	void SetFocusedLocally(bool bFocused, const FInteract_VerbAvailability& Avail);

	/** Whether this prompt is currently focused by the local viewer. */
	UFUNCTION(BlueprintPure, Category = "Interact|WorldPrompt")
	bool IsFocusedLocally() const { return bFocusedLocally; }

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Socket on the owner to anchor the prompt to (NAME_None = actor origin). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|WorldPrompt")
	FName SocketName = NAME_None;

	/** Local-space offset (cm) added to the anchor location when reporting the world location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|WorldPrompt")
	FVector LocalOffset = FVector(0.f, 0.f, 80.f);

	/** Default marker/style tag shown when the prompt is focused and the verb is available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|WorldPrompt", meta = (Categories = "DP.HUD.Marker"))
	FGameplayTag PromptStyleTag;

	/**
	 * Marker/style tag shown when the focused verb is UNAVAILABLE (locked/etc). Empty = reuse
	 * PromptStyleTag. Lets the HUD pick a "denied" icon without the marker knowing the gameplay reason.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|WorldPrompt", meta = (Categories = "DP.HUD.Marker"))
	FGameplayTag UnavailableStyleTag;

	/** When true the prompt registers as a trackable only while locally focused (off-map otherwise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|WorldPrompt")
	bool bVisibleOnlyWhenFocused = true;

private:
	/** Resolve the world marker registry for this component's world (null in editor/preview). */
	UHUD_MarkerRegistrySubsystem* ResolveRegistry() const;

	/** Re-broadcast the registry's change delegate so a live ViewModel refreshes after a state change. */
	void NotifyRegistryChanged();

	/** Cached registry resolved at BeginPlay so EndPlay unregisters from the same instance. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UHUD_MarkerRegistrySubsystem> RegistryWeak;

	/** True between successful register and unregister. */
	bool bRegistered = false;

	/** Local focus state pushed by the focus owner. Drives IsVisibleOnMap + which style tag is shown. */
	bool bFocusedLocally = false;

	/** Whether the focused verb is currently available (drives the available vs unavailable style). */
	bool bFocusedVerbAvailable = true;
};
