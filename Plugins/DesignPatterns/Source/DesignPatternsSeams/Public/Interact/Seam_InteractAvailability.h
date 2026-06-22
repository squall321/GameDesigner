// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Seam_InteractAvailability.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_InteractAvailability : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional usability/availability bridge for an interactable.
 *
 * The Interaction module never knows WHY a verb is usable or not — it only asks this seam. Gameplay
 * systems that gate interactions (door locks, key-rings, an economy wallet, a skill/cooldown gate,
 * a "container full" check) implement this on the interactable object so the interactor can fold a
 * reason into its prompt without hard-depending on any of those systems.
 *
 * CONTRACT
 *   - References ONLY Core / GameplayTags / FText and the existing ISeam_EntityIdentity, so
 *     DesignPatternsSeams stays a dependency-free leaf (no FInteract_* type ever crosses this seam).
 *   - The reason is carried purely as an FGameplayTag under DP.Interact.Reason.* (Locked / NoKey /
 *     TooFar / NotEnoughResource / OnCooldown / Full). No enum, no Interaction-module struct.
 *   - The instigator is passed by identity (TScriptInterface<ISeam_EntityIdentity>) rather than a
 *     concrete pawn type, so the availability system keys off the stable entity id.
 *   - Both methods are READ-ONLY / side-effect free: IsVerbAvailable runs on clients for prompt
 *     greying as well as on the server before it commits the interaction.
 *
 * House style: this is a project-supplied / gameplay-facing bridge, so it uses BlueprintNativeEvent
 * (matching ISeam_PlatformAchievements). The accompanying .cpp provides no-op default bodies so an
 * implementer can override only what it cares about, and the framework is safe when the seam is
 * absent (the consumer treats an unoverridden default as "available").
 */
class DESIGNPATTERNSSEAMS_API ISeam_InteractAvailability
{
	GENERATED_BODY()

public:
	/**
	 * Is the given verb usable right now by Instigator? Returns true when usable; when false,
	 * OutReasonTag is set to a child of DP.Interact.Reason describing why.
	 *
	 * @param Verb         The verb being queried (a child of DP.Data.Interact.Verb).
	 * @param Instigator   The acting entity, by identity. May be an empty interface (anonymous query).
	 * @param OutReasonTag Set to the DP.Interact.Reason.* tag when the result is false; cleared when true.
	 * @return true if the verb is currently available.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Interact")
	bool IsVerbAvailable(FGameplayTag Verb, const TScriptInterface<ISeam_EntityIdentity>& Instigator, FGameplayTag& OutReasonTag) const;

	/**
	 * Map a (verb, reason) pair to player-facing text (e.g. "Requires Brass Key", "On Cooldown").
	 * Implementers may ignore Verb and key purely off ReasonTag. Returns empty FText when the seam
	 * has nothing to add (the consumer then uses its own default text for the reason tag).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Interact")
	FText GetUnavailableReasonText(FGameplayTag Verb, FGameplayTag ReasonTag) const;
};
