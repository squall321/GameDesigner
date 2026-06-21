// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Action/DPGameplayActionLite.h"
#include "GameplayTagContainer.h"
#include "Move_DashAction.generated.h"

class UDP_GameplayActionComponent;
class UMove_StaminaComponent;

/**
 * The dash/dodge lightweight action. It is the SINGLE OWNER of the shared i-frame tag lifetime: on
 * Activate it adds the loose owned tag "DP.Combat.Status.IFrame" to the owner's
 * UDP_GameplayActionComponent (replicated + authority-guarded by that component) and schedules a timer to
 * remove it after the i-frame window; on EndAction (cancel or completion) it force-removes the tag so it
 * can never leak. Cooldown is enforced by the granting component via the inherited CooldownDuration.
 *
 * The dash TRANSLATION is performed by UMove_State_Dash; this action's job is purely cost + i-frame +
 * cooldown bookkeeping. Combat's UCombat_IFrameAwareDamageExecution is what actually mitigates damage to
 * zero while the tag is present — Movement never implements an invulnerability system, it only adds the
 * agreed tag. CanActivate additionally checks stamina via ISeam_NeedProvider (resolved off the owner).
 *
 * Cosmetic-prediction note: clients may locally add the tag for immediate feedback, but the
 * UDP_GameplayActionComponent guards the replicated owned-tag set on authority, so the authoritative
 * i-frame state is server-driven.
 */
UCLASS(Blueprintable)
class DESIGNPATTERNSMOVEMENT_API UMove_DashAction : public UDP_GameplayActionLite
{
	GENERATED_BODY()

public:
	UMove_DashAction();

	//~ Begin UDP_GameplayActionLite
	virtual bool CanActivate_Implementation(const FDP_ActionActivationData& Data) const override;
	virtual bool Activate_Implementation(const FDP_ActionActivationData& Data) override;
	virtual void EndAction_Implementation(const FDP_ActionActivationData& Data, bool bWasCancelled) override;
	//~ End UDP_GameplayActionLite

	/** Flat stamina cost; <= 0 means "use the stamina component's profile/settings dash cost". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Movement|Dash", meta = (ClampMin = "0.0"))
	float StaminaCostOverride = 0.f;

	/** Dash duration (seconds); <= 0 -> project settings fallback. Also the i-frame window upper bound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Movement|Dash", meta = (ClampMin = "0.0"))
	float DashDurationOverride = 0.f;

	/**
	 * Fraction [0,1] of the dash duration granting i-frames; <= 0 -> project settings fallback. 1 = the
	 * whole dash is invulnerable; smaller values grant a brief invulnerability window at the start.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Movement|Dash", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IFrameFractionOverride = 0.f;

private:
	/** Timer that removes the i-frame tag at the end of its window. Cleared on EndAction. */
	FTimerHandle IFrameTimerHandle;

	/** Resolve the owner's action component (the loose-tag carrier). */
	UDP_GameplayActionComponent* ResolveActionComponent(const FDP_ActionActivationData& Data) const;

	/** Resolve the owner's stamina component for the cost check/drain. */
	UMove_StaminaComponent* ResolveStaminaComponent(const FDP_ActionActivationData& Data) const;

	/** Effective stamina cost (override -> stamina component dash cost). */
	float ResolveStaminaCost(const FDP_ActionActivationData& Data) const;

	/** Effective dash duration (override -> settings). */
	float ResolveDashDuration() const;

	/** Effective i-frame fraction (override -> settings). */
	float ResolveIFrameFraction() const;

	/** Remove the i-frame tag (called by the timer and defensively on EndAction). */
	void RemoveIFrameTag();
};
