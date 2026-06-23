// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_EncounterDirector.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_EncounterDirector : public UInterface
{
	GENERATED_BODY()
};

/**
 * PROJECT-BRIDGE seam onto a world encounter / spawn director.
 *
 * Lets a producer (e.g. the LevelDirector pacing subsystem) drive encounters WITHOUT a hard
 * dependency on the AI module's concrete spawn-director types. A thin AI-side adapter implements
 * this seam, maps the abstract EncounterId tag to its own UAI_EncounterDataAsset via the data
 * registry, and forwards to the real director (ActivateEncounter(asset, ProgressionInput) /
 * StopEncounter() / IsEncounterActive()). The adapter self-registers a
 * TScriptInterface<ISeam_EncounterDirector> under a service-locator key owned by the AI module
 * (DP.Service.AI.EncounterDirector); consumers resolve it weakly and re-resolve on use.
 *
 * BlueprintNativeEvent UINTERFACE (project-bridge / UI-class seam house style — like
 * ISeam_ActivationGate and ISeam_Persistable) so it is resolvable as a TScriptInterface and a
 * project may implement it in Blueprint. The shipped default implementation here is INERT (all
 * methods are no-ops returning false), so a project with no AI spawn director still links and the
 * pacing producer simply no-ops — keeping the seam genre-agnostic and removable.
 *
 * SIGNATURE NOTE: this is intentionally NARROW — it mirrors exactly what a single-encounter spawn
 * director can satisfy. There is NO per-region "is running" query and NO separate budget scalar:
 * the director exposes a single global active state and a single ProgressionInput (0..1) sampled at
 * activation. A producer that wants to change live intensity re-activates with a new ProgressionInput
 * (the adapter treats a re-activation of the same encounter as an intensity update).
 *
 * AUTHORITY: activation/stop are authoritative mutations of world spawn state — the IMPLEMENTER is
 * expected to guard authority internally; the producer (LevelDirector pacing) is itself authority-only
 * so it never calls these on a client.
 */
class DESIGNPATTERNSSEAMS_API ISeam_EncounterDirector
{
	GENERATED_BODY()

public:
	/**
	 * Begin (or update) the encounter identified by EncounterId for the logical RegionTag, at the
	 * given normalized ProgressionInput (0..1 — early/calm .. late/intense). The adapter resolves
	 * EncounterId to its concrete encounter definition; RegionTag is opaque routing context the
	 * adapter may use to scope spawning. Re-calling with the same EncounterId is an intensity update.
	 *
	 * @param RegionTag        Logical region/owner the encounter belongs to (routing context).
	 * @param EncounterId      Stable design-time id the adapter maps to a concrete encounter asset.
	 * @param ProgressionInput Normalized 0..1 pacing input; the adapter clamps and consumes it.
	 * @return true if the director accepted the activation (false if unresolved/invalid/no authority).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Encounter")
	bool ActivateEncounterForRegion(FGameplayTag RegionTag, FGameplayTag EncounterId, float ProgressionInput);

	/**
	 * Stop the encounter associated with RegionTag (the single-encounter director stops its current
	 * encounter). @return true if an encounter was stopped.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Encounter")
	bool StopEncounter(FGameplayTag RegionTag);

	/** True if the director currently has an active encounter (single global state). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Encounter")
	bool IsEncounterActive() const;
};
