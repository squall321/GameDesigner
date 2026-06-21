// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Respawn/GM_RespawnTypes.h"
#include "GM_RespawnComponent.generated.h"

class UGM_RespawnComponent;

/** Fired on the authority after the owning actor has been repositioned to a respawn point. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGM_OnRespawned, UGM_RespawnComponent*, Component, FTransform, SpawnTransform);

/**
 * Authority-side respawn driver placed on a respawnable actor (typically a pawn/character).
 *
 * On RequestRespawn (authority) it applies the respawn rules — an optional delay, a per-actor budget,
 * and team-filtered spawn-point selection — then repositions the owning actor and fires OnRespawned.
 * Spawn points come ENTIRELY from the LevelDirector spawn-region seam: the component resolves an
 * ILvl_SpawnRegionProvider from the service locator under LvlTags::Service_SpawnRegionProvider and asks
 * it for transforms filtered by the actor's team tag. It NEVER hard-depends on any concrete spawn-volume
 * type, and degrades to a documented inert default (no respawn, EGM_RespawnResult::NoSpawnPoint) when no
 * provider is registered.
 *
 * ALL state-changing paths are authority-guarded at the top and early-return on clients (HARD RULE 5).
 * This component holds NO replicated state itself — repositioning the owning actor (SetActorTransform)
 * replicates through the actor's normal movement replication; the component is pure server-side logic.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentTick, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSGAMEMODE_API UGM_RespawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGM_RespawnComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Authority-only: queue (or, with zero delay, immediately perform) a respawn of the owning actor.
	 * Early-returns on clients. Applies the budget and pending-guard rules, then either schedules the
	 * respawn after the resolved delay or performs it now.
	 *
	 * @param OverrideDelaySeconds  If >= 0, overrides the settings/instance default respawn delay.
	 * @return The result. Succeeded means queued or performed; any other value means no respawn happened.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Respawn")
	EGM_RespawnResult RequestRespawn(float OverrideDelaySeconds = -1.0f);

	/** Authority-only: cancel a pending respawn (clears the timer). No-op on clients / when none pending. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Respawn")
	void CancelPendingRespawn();

	/** True if a respawn is currently scheduled (timer running) for this actor. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Respawn")
	bool IsRespawnPending() const;

	/** How many times this actor has been respawned by this component so far. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Respawn")
	int32 GetRespawnCount() const { return RespawnCount; }

	/** Fired on authority once the owning actor has been repositioned. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|GameMode|Respawn")
	FGM_OnRespawned OnRespawned;

	/**
	 * Per-instance override of the respawn delay (seconds). Negative means "use the settings default".
	 * A designer may set this on the component to give specific actor classes a different delay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|GameMode|Respawn",
		meta = (Units = "s"))
	float InstanceRespawnDelaySeconds = -1.0f;

	/**
	 * Optional override of the spawn-point filter tag. When unset (empty) the component filters by the
	 * actor's team tag (read from its UGM_TeamComponent). Lets a designer pin an actor to a specific
	 * spawn role/region regardless of team.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|GameMode|Respawn")
	FGameplayTag SpawnFilterOverride;

protected:
	/** Timer-fired (or immediate) authority respawn. Selects a point and repositions the owner. */
	void PerformRespawn();

	/**
	 * Resolve an eligible spawn transform for this actor by querying the spawn-region seam, filtered by
	 * the actor's team (or SpawnFilterOverride). Returns false (and leaves OutTransform untouched) if no
	 * provider is registered or no point matched.
	 */
	bool SelectSpawnTransform(FGameplayTag FilterTag, FTransform& OutTransform) const;

	/** The filter tag to use for spawn-point selection (override if set, else the team tag). */
	FGameplayTag ResolveSpawnFilterTag() const;

	/** The respawn delay this request should use (override > instance > settings default). */
	float ResolveRespawnDelay(float OverrideDelaySeconds) const;

	/** Authority check delegated to the owning actor (early-out on clients). */
	bool HasAuthority() const;

private:
	/** Handle for the pending respawn timer. */
	FTimerHandle RespawnTimerHandle;

	/** Count of completed respawns (1-based when reported in the payload). */
	int32 RespawnCount = 0;

	/** Cached filter for the in-flight respawn (captured at request time so a mid-delay team change is honored at perform time only if re-read). */
	FGameplayTag PendingFilterTag;
};
