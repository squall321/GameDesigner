// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Needs/Seam_NeedProvider.h"
#include "Move_StaminaComponent.generated.h"

class UMove_StaminaProfile;
class ISeam_SimClock;

/** Broadcast (locally, on every peer via OnRep) when the replicated stamina value changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMove_OnStaminaChanged, float, NewStamina, float, MaxStamina);

/** Broadcast (locally) the first frame stamina reaches zero (cosmetic exhaustion feedback hook). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMove_OnStaminaDepleted);

/**
 * The authoritative stamina meter for a character, and the PRIMARY owner of the shared "need" seam.
 *
 * Implements ISeam_NeedProvider answering Move.Need.Stamina, so Combat (guard/defense) and Survival
 * (crafting gates) read the SAME meter through a TScriptInterface<ISeam_NeedProvider> instead of each
 * inventing its own. Per the architecture plan this resolves the "two stamina/need systems" overlap.
 *
 * Replication (HARD RULE 4): the float Stamina is replicated (ReplicatedUsing) on this UActorComponent
 * carrier; every mutator (TryDrain / Restore / regen tick) guards HasAuthority() at the TOP. Clients
 * read the replicated value for UI and gating predictions; they never write it. Regen honors the shared
 * ISeam_SimClock (pauses while paused, scales by time scale) when one is resolvable.
 */
UCLASS(ClassGroup = (DesignPatternsMovement), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSMOVEMENT_API UMove_StaminaComponent : public UActorComponent, public ISeam_NeedProvider
{
	GENERATED_BODY()

public:
	UMove_StaminaComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_NeedProvider
	/** Normalized [0,1] stamina for Move.Need.Stamina; 0 for any other need tag. */
	virtual float GetNeedNormalized_Implementation(FGameplayTag NeedTag) const override;
	/** True only for Move.Need.Stamina. */
	virtual bool SupportsNeed_Implementation(FGameplayTag NeedTag) const override;
	/** Appends Move.Need.Stamina. */
	virtual void GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const override;
	//~ End ISeam_NeedProvider

	// ---- Queries (safe on clients) ----

	/** Current stamina (replicated). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement|Stamina")
	float GetStamina() const { return Stamina; }

	/** Effective maximum stamina (profile, falling back to settings). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement|Stamina")
	float GetMaxStamina() const;

	/** True if at least Amount stamina is available right now. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement|Stamina")
	bool HasStamina(float Amount) const { return Stamina >= Amount; }

	/**
	 * True if the character is in the post-exhaustion lockout (stamina hit zero and has not yet
	 * recovered past the profile's recovery threshold). Sprint/dash should be blocked while true.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement|Stamina")
	bool IsExhausted() const { return bExhausted; }

	/** The effective flat dash cost (profile, falling back to settings). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement|Stamina")
	float GetDashCost() const;

	// ---- Authoritative mutators (guard HasAuthority at the TOP) ----

	/**
	 * Attempt to drain Amount stamina. Authority-only.
	 * @return true if there was enough stamina and it was drained; false (no change) otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Stamina")
	bool TryDrain(float Amount);

	/** Add stamina (e.g. a consumable). Authority-only. Clamped to [0, Max]. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Stamina")
	void Restore(float Amount);

	/**
	 * Mark this tick as actively draining (e.g. while sprinting) so regen is suppressed and the regen
	 * delay timer resets. Called by the sprint state on authority each tick it consumes stamina.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Stamina")
	void NotifyDraining();

	// ---- Events ----

	/** Fires on every peer when stamina changes (authority set or client OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Movement|Stamina")
	FMove_OnStaminaChanged OnStaminaChanged;

	/** Fires when stamina first reaches zero. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Movement|Stamina")
	FMove_OnStaminaDepleted OnStaminaDepleted;

	/** Tuning asset; falls back to project settings field-by-field when absent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Movement|Stamina")
	TObjectPtr<UMove_StaminaProfile> Profile;

protected:
	/** Replicated stamina value. Mutated only on authority; clients react via OnRep. */
	UPROPERTY(ReplicatedUsing = OnRep_Stamina, VisibleInstanceOnly, Category = "DesignPatterns|Movement|Stamina")
	float Stamina = 0.f;

	/** Replicated exhaustion-lockout flag (set on authority when stamina hits zero). */
	UPROPERTY(ReplicatedUsing = OnRep_Stamina)
	bool bExhausted = false;

	/** Client reaction to a replicated stamina/exhaustion change: fire the change/deplete delegates. */
	UFUNCTION()
	void OnRep_Stamina();

private:
	/** Authoritative regen accumulator: world time at which regen may resume (after the regen delay). */
	float RegenAllowedTime = 0.f;

	/** True only on the authority for the tick in which a drain was requested (suppresses regen). */
	bool bDrainedThisTick = false;

	/** Cached resolved sim clock (weak; may be null in projects without one). Re-resolved if stale. */
	TWeakObjectPtr<UObject> CachedClockObject;

	/** Resolve the shared sim clock from the service locator, or null. Caches the result. */
	TScriptInterface<ISeam_SimClock> ResolveSimClock();

	/** Effective regen-per-second (profile -> settings). */
	float GetRegenPerSecond() const;

	/** Effective regen delay (profile -> settings). */
	float GetRegenDelay() const;

	/** Effective exhaustion recovery threshold (absolute stamina; 0 disables the gate). */
	float GetExhaustionRecoveryThreshold() const;

	/** Apply a new stamina value on authority: clamp, update exhaustion, mark dirty, fire local delegate. */
	void SetStaminaAuthoritative(float NewValue);

	/** Register/unregister this provider with the service locator (per settings flag). */
	void RegisterAsService(bool bRegister);
};
