// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Relationship/Ent_EntityLinkArray.h"
#include "Ent_RelationshipComponent.generated.h"

class UEnt_EntityComponent;

/**
 * Fired (server and clients) after this entity's outgoing link set changes — a link was added/removed
 * on authority, or replicated link state arrived on a client.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnt_OnLinksChanged, UEnt_RelationshipComponent*, RelationshipComponent);

/**
 * Authoritative replicated carrier of an entity's OUTGOING relationship links.
 *
 * Sits beside the entity spine (UEnt_EntityComponent) on an entity actor and owns a delta-replicated
 * fast-array of directed links (owner / parent / attached / grouped, all FGameplayTag-keyed). The
 * world relationship subsystem (UEnt_RelationshipSubsystem) indexes these on every machine so other
 * systems can query the graph by stable id.
 *
 * AUTHORITY MODEL
 *  Every mutator authority-guards at the TOP (via the sibling UEnt_EntityComponent::HasEntityAuthority)
 *  and early-returns on clients — clients only ever observe links through the fast-array + OnLinksChanged.
 *
 * IDENTITY
 *  This component reads its OWN id through ISeam_EntityIdentity on the sibling UEnt_EntityComponent (the
 *  correct identity seam), located via IEnt_Entity::Execute_GetEntityComponent off the owner. It never
 *  generates an id of its own.
 *
 * REPLICATION
 *  Links is the only replicated state. It is a fast-array (FInstancedStruct never replicates; the per-link
 *  scalar rides FSeam_NetValue). OnRep_Links re-registers the local index mirror and fires OnLinksChanged.
 */
UCLASS(ClassGroup = (DesignPatternsEntity), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Cooking, Collision))
class DESIGNPATTERNSENTITY_API UEnt_RelationshipComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnt_RelationshipComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// ---- Mutators (AUTHORITY ONLY) -------------------------------------------------------------

	/**
	 * Add (or update the Param of) a directed link of kind LinkKindTag to TargetId. AUTHORITY ONLY.
	 * A link of kind Ent.Link.Owner is one-to-one: adding one replaces any existing owner link.
	 * No-op on clients / for an invalid target or kind. Returns true if the set changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	bool AddLink(FGameplayTag LinkKindTag, FSeam_EntityId TargetId, FSeam_NetValue Param);

	/** Remove the link of kind LinkKindTag to TargetId. AUTHORITY ONLY. Returns true if removed. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	bool RemoveLink(FGameplayTag LinkKindTag, FSeam_EntityId TargetId);

	/** Remove every outgoing link of kind LinkKindTag. AUTHORITY ONLY. Returns the number removed. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	int32 RemoveAllLinksOfKind(FGameplayTag LinkKindTag);

	/** Remove every outgoing link (all kinds). AUTHORITY ONLY. Returns the number removed. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	int32 ClearAllLinks();

	// ---- Queries (safe on clients) -------------------------------------------------------------

	/** Append the targets of every outgoing link of kind KindTag (invalid kind = all) into Out. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Relationship")
	void GetLinks(FGameplayTag KindTag, TArray<FSeam_EntityId>& Out) const;

	/** The target of the single owner-kind link (Ent.Link.Owner), or an invalid id. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Relationship")
	FSeam_EntityId GetPrimaryOwner() const;

	/** True if there is an outgoing link to TargetId of kind KindTag (invalid kind = any kind). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Relationship")
	bool HasLinkTo(FSeam_EntityId TargetId, FGameplayTag KindTag) const;

	/** Flatten every outgoing link into plain save/snapshot records. */
	void GetLinkRecords(TArray<FEnt_LinkRecord>& Out) const;

	/**
	 * Authoritatively replace ALL outgoing links from a set of records (used by snapshot restore).
	 * AUTHORITY ONLY. Clears existing links then re-adds. No-op on clients.
	 */
	void RestoreFromRecords(const TArray<FEnt_LinkRecord>& Records);

	/** This entity's stable id (read from the sibling entity component), or invalid if not yet known. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Relationship")
	FSeam_EntityId GetOwnEntityId() const;

	// ---- Lifetime propagation policy -----------------------------------------------------------

	/**
	 * When true, destroying this entity's owner actor cascades to entities that name THIS entity as
	 * their primary owner (the world subsystem performs the cascade on authority). Tunable per entity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Relationship")
	bool bDestroyChildrenWithParent = false;

	/** Fired (server and clients) when the outgoing link set changes. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Relationship")
	FEnt_OnLinksChanged OnLinksChanged;

	/** Called by the fast-array entry callbacks on clients to surface a replicated link change. */
	void HandleReplicatedLinkChange();

protected:
	/** RepNotify: link state arrived/changed on a client. */
	UFUNCTION()
	void OnRep_Links();

private:
	/** The replicated outgoing links. The only replicated state on this component. */
	UPROPERTY(ReplicatedUsing = OnRep_Links)
	FEnt_EntityLinkArray Links;

	/** Cached sibling entity component (resolved off the owner via IEnt_Entity); non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnt_EntityComponent> CachedEntityComponent;

	/** True once we have registered this component's links with the world relationship subsystem. */
	bool bRegisteredWithIndex = false;

	/** Resolve (and cache) the sibling entity component off the owner. */
	UEnt_EntityComponent* ResolveEntityComponent() const;

	/** True when this machine owns authority over the entity (delegates to the sibling component). */
	bool HasEntityAuthority() const;

	/** (Re)register every current link into the world relationship subsystem (server + clients). */
	void RegisterAllLinksWithIndex();

	/** Unregister every current link from the world relationship subsystem. */
	void UnregisterAllLinksFromIndex();

	/** Broadcast the local "links changed" bus message for this entity. */
	void BroadcastLinksChanged() const;
};
