// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Reputation/Seam_FactionStanding.h"
#include "Hub/WorldHub_Scope.h"
#include "Net/Seam_NetValue.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "Faction/WorldHub_FactionTypes.h"
#include "WorldHub_FactionMatrixComponent.generated.h"

class UWorldHub_StateHubSubsystem;
class UWorldHub_FactionMatrixDataAsset;

/**
 * FACTION relationship matrix, AUTHORITY-side, on the always-relevant carrier actor.
 *
 * SINGLE MECHANISM: every (A,B) standing is PROJECTED into the hub's authoritative registry under a
 * composed FGameplayTag key (DP.WorldHub.Faction.Standing.<A>.<B>) via SetNetValue, so it replicates
 * through the EXISTING UWorldHub_StateRepComponent fast-array AND persists through the EXISTING
 * snapshot/save path — there is NO sibling fast-array. A small in-memory index mirrors the projected
 * values for fast reads on both server and client (kept in sync from the hub's OnValueChanged).
 *
 * Implements ISeam_FactionStanding (raw virtuals) so AI / economy / narrative read standings without
 * including this header; it self-registers under DP.Service.WorldHub.FactionMatrix (WeakObserved).
 *
 * Authoritative writes (Authority_Set/AdjustStanding) clamp via the data asset and guard authority at
 * the TOP. Clients never write — client intent routes through UWorldHub_IntentComponent.
 */
UCLASS(ClassGroup = (DesignPatternsWorld), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSWORLD_API UWorldHub_FactionMatrixComponent
	: public UActorComponent
	, public ISeam_FactionStanding
{
	GENERATED_BODY()

public:
	UWorldHub_FactionMatrixComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** The authored matrix configuration (defaults, bounds, tiers, symmetry, replicate/save policy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	TObjectPtr<UWorldHub_FactionMatrixDataAsset> InitialMatrix;

	// ---- Authoritative mutators (guard authority at TOP) --------------------------------------

	/**
	 * Set A's standing toward B to Standing (clamped). AUTHORITY ONLY. Projects to the hub (and mirrors
	 * B->A when the matrix is symmetric). @return the stored (clamped) value (0 on a client / no hub).
	 */
	float Authority_SetStanding(FGameplayTag A, FGameplayTag B, float Standing);

	/** Add Delta to A's standing toward B (clamped). AUTHORITY ONLY. @return the new (clamped) value. */
	float Authority_AdjustStanding(FGameplayTag A, FGameplayTag B, float Delta);

	// ---- ISeam_FactionStanding (raw virtuals) -------------------------------------------------
	virtual bool HasFactionMatrix() const override;
	virtual float GetStanding(FGameplayTag FactionA, FGameplayTag FactionB) const override;
	virtual FGameplayTag GetStandingTier(FGameplayTag FactionA, FGameplayTag FactionB) const override;
	virtual bool AreHostile(FGameplayTag FactionA, FGameplayTag FactionB) const override;

	/** Blueprint-facing forwarder to the interface read (so BP can read without an interface cast). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Faction")
	float BP_GetStanding(FGameplayTag A, FGameplayTag B) const { return GetStanding(A, B); }

	/** Fired (server and clients) when a standing changes (driven from the hub's OnValueChanged). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|Faction")
	FWorldHub_OnStandingChanged OnStandingChanged;

	/**
	 * Address an (A,B) standing as a (Key, Scope) hub slot using ONLY already-valid faction tags (no
	 * need to pre-register a synthetic composed tag): the slot's Scope is Faction(A) and its Key is the
	 * faction tag B. This is unambiguous because the only projected value per (A,B) is the standing, and
	 * it lets standings ride the existing rep carrier and snapshot/save path verbatim.
	 *
	 * @return true (filling OutKey/OutScope) when both faction tags are valid.
	 */
	static bool AddressStanding(const FGameplayTag& A, const FGameplayTag& B, FGameplayTag& OutKey, FWorldHub_Scope& OutScope);

private:
	/** Authority check delegated to the hub. */
	bool HasAuthority() const;

	/** Resolve / cache the world hub and bind its OnValueChanged. */
	UWorldHub_StateHubSubsystem* ResolveHub();

	/** Apply InitialStandings into the hub (AUTHORITY ONLY) and ensure the flag policy is registered. */
	void ApplyInitialMatrix();

	/** Project one (A,B) standing into the hub registry under the composed key (AUTHORITY path). */
	void ProjectStanding(const FGameplayTag& A, const FGameplayTag& B, float Standing);

	/** Bound to the hub's OnValueChanged: refresh the read index and fire OnStandingChanged for our keys. */
	UFUNCTION()
	void OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue);

	/** Self-(un)register under DP.Service.WorldHub.FactionMatrix. */
	void RegisterSelfAsService(bool bRegister);

	/** The hub standings project into (re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> Hub;

	/**
	 * Fast in-memory read index: (Scope=Faction(A), Key=B) -> standing. Mirrors the projected hub slots
	 * and is kept in sync on BOTH server and client from OnValueChanged, so reads never touch the hub on
	 * the hot path. Not replicated (the hub's rep carrier is the replication mechanism).
	 */
	TMap<FWorldHub_SlotAddress, float> StandingIndex;
};
