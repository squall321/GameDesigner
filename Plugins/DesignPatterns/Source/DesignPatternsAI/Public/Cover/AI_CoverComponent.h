// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_CoverComponent.generated.h"

class AAI_CoverPoint;
class UAI_CoverSubsystem;
class IDP_BlackboardProvider;

/**
 * Fired locally when this agent acquires / releases cover (authority and via OnRep on clients).
 * @param bInCover True once a cover point is claimed; false on release.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAI_OnCoverStateChanged, bool, bInCover);

/**
 * Per-agent cover BEHAVIOUR. Composes the world cover system: searches via UAI_CoverSubsystem, claims a
 * point, and writes the cover location to the agent blackboard so the FSM/strategies move there.
 *
 * AUTHORITY: ALL search/claim/blackboard writes are server-only (HasAuthoritySafe() guarded at the TOP) —
 * the FSM store is server-authoritative. The component replicates only a tiny cosmetic projection
 * (ClaimedCoverLocation + bPeeking) via one OnRep so clients can play peek/lean cosmetics; nothing about
 * the authoritative decision rides on the client.
 *
 * Threat direction is derived locally from the owner's IAI_Threatened top threat + the perception
 * component's last-known target location (both already on the agent), so cover faces the real danger.
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_CoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAI_CoverComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Search for and claim the best cover near the owner from its current threat. AUTHORITY ONLY.
	 * Writes the cover location into the blackboard and releases any previously-held point.
	 * @return true if a cover point was claimed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Cover")
	bool AcquireCover();

	/** Release the currently-held cover point (if any). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Cover")
	void ReleaseCover();

	/** Begin a peek (cosmetic + blackboard flag). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Cover")
	void BeginPeek();

	/** End a peek. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Cover")
	void EndPeek();

	/** @return true if the agent currently holds a cover point. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	bool IsInCover() const { return HeldCoverId.IsValid(); }

	/** @return the world location of the held cover (zero if none). Safe on clients (cosmetic projection). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	FVector GetClaimedCoverLocation() const { return ClaimedCoverLocation; }

	/** Fired locally on cover acquire/release. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|AI|Cover")
	FAI_OnCoverStateChanged OnCoverStateChanged;

	// ---- Config (tunables; no magic gameplay numbers in code) ----

	/** Maximum radius (world units) searched around the owner for cover. <= 0 means "no radius limit". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Cover", meta = (ClampMin = "0.0"))
	float SearchRadius = 1500.f;

	/** Blackboard key the claimed cover world location is written under (vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Cover")
	FName BlackboardKey_CoverLocation = TEXT("AI.CoverLocation");

	/** Blackboard key a bool is written under to signal "I am currently in cover". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Cover")
	FName BlackboardKey_InCover = TEXT("AI.InCover");

	/** When true, cover claim/release is republished on the core bus (DP.Bus.AI.Cover.Claimed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Cover")
	bool bBroadcastOnBus = true;

	/**
	 * Fallback threat location used when neither a top-threat nor a perceived target can be resolved (the
	 * owner's forward * this distance), so cover still picks a sensible facing. Defensive, designer-tunable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Cover", meta = (ClampMin = "1.0"))
	float FallbackThreatForwardDistance = 1000.f;

protected:
	/** Cosmetic projection of the held cover location; replicated so clients can position peek cosmetics. */
	UPROPERTY(ReplicatedUsing = OnRep_CoverState, Transient)
	FVector_NetQuantize ClaimedCoverLocation = FVector::ZeroVector;

	/** Cosmetic peek flag; replicated for client peek/lean cosmetics. */
	UPROPERTY(ReplicatedUsing = OnRep_CoverState, Transient)
	bool bPeeking = false;

	/** Client reaction to a replicated cover-state change: fire the delegate. */
	UFUNCTION()
	void OnRep_CoverState();

private:
	/** Stable id of the held cover point (authority bookkeeping; not replicated — clients read the location). */
	UPROPERTY(Transient)
	FSeam_EntityId HeldCoverId;

	/** Resolve the world cover subsystem (null-safe). */
	UAI_CoverSubsystem* ResolveCoverSubsystem() const;

	/** Resolve the owner's blackboard provider (on the owner or one of its components). */
	IDP_BlackboardProvider* ResolveBlackboardProvider() const;

	/** Derive the current threat world location for cover facing (top threat / perception / fallback). */
	FVector ResolveThreatLocation() const;

	/** Resolve the owner's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** Push the cover location + in-cover flag into the blackboard (authority only). */
	void PushCoverToBlackboard(const FVector& Location, bool bInCover);

	/** Broadcast a flat cover-claim payload on the core bus. */
	void BroadcastCoverOnBus(const FSeam_EntityId& CoverId, FGameplayTag CoverTypeTag, bool bClaimed, const FVector& Location) const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
