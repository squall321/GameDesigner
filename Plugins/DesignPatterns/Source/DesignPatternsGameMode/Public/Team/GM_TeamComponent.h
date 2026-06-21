// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GM_TeamComponent.generated.h"

class UGM_TeamComponent;

/** Multicast notification raised on every machine when this component's replicated team tag changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGM_OnTeamChanged, UGM_TeamComponent*, Component,
	FGameplayTag, PreviousTeam, FGameplayTag, NewTeam);

/**
 * Per-actor team membership carrier — the replicated home of an actor's team tag.
 *
 * Placed on a pawn, character or player controller. The team tag is the single replicated piece of state
 * (ReplicatedUsing=OnRep_TeamTag) so every client knows the actor's team for coloring, targeting and
 * friendly-fire decisions. WRITES are authority-only: SetTeamTag early-returns on clients (HARD RULE 5),
 * and the canonical assignment/balancing entry point is UGM_TeamSubsystem on the server.
 *
 * This component is a CARRIER, not a policy owner: it does not decide who is friendly with whom. The
 * friendly/hostile relation is resolved by UGM_TeamSubsystem (the ISeam_TeamAffinity provider) which
 * reads this component's tag off each actor. Consumers (combat/AI/HUD) should go through the seam rather
 * than reading this component directly, so they stay decoupled from the GameMode module.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentTick, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSGAMEMODE_API UGM_TeamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGM_TeamComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/**
	 * Authority-only setter for this actor's team. Early-returns on clients. Stores the new tag,
	 * triggers OnRep locally (server does not get OnRep automatically) so the OnTeamChanged delegate
	 * fires consistently on every machine, and lets the owning subsystem broadcast the bus event.
	 *
	 * @param NewTeamTag  The team tag to assign (empty clears team membership).
	 * @return True if the value changed (and thus replicated); false if it was already equal or we are a client.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|GameMode|Team")
	bool SetTeamTag(FGameplayTag NewTeamTag);

	/** The actor's current team tag (empty = no team). Valid on clients (replicated). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag GetTeamTag() const { return TeamTag; }

	/** True if this actor currently belongs to a (valid, non-empty) team. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team")
	bool HasTeam() const { return TeamTag.IsValid(); }

	/** Convenience: locate the team component on an actor (returns null if absent). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|GameMode|Team", meta = (DefaultToSelf = "Actor"))
	static UGM_TeamComponent* FindOn(const AActor* Actor);

	/** Fired on every machine when TeamTag changes (after replication on clients, immediately on server). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|GameMode|Team")
	FGM_OnTeamChanged OnTeamChanged;

protected:
	/** Replication callback: clients learn the new team here. Drives OnTeamChanged with the delta. */
	UFUNCTION()
	void OnRep_TeamTag(FGameplayTag PreviousTeam);

private:
	/**
	 * The replicated team tag. Authority-written only. ReplicatedUsing so clients raise OnTeamChanged;
	 * the server raises it manually from SetTeamTag (RepNotify does not fire on the authority).
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TeamTag, VisibleInstanceOnly, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag TeamTag;
};
