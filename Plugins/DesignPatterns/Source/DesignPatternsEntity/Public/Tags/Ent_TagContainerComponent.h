// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Tags/Ent_ReplicatedTagArray.h"
#include "Ent_TagContainerComponent.generated.h"

class UEnt_EntityComponent;

/**
 * Fired (server and clients) after the replicated tag set changes.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEnt_OnTagChanged, UEnt_TagContainerComponent*, Component, FGameplayTag, Tag);

/**
 * Network-authoritative entity tag container.
 *
 * Complements the single-machine UEnt_TagSetTrait: where the trait holds local/save tags on a
 * non-replicated subobject, this component owns a delta-replicated fast-array of tags (with optional
 * stack counts) and registers the entity with the world query subsystem for radius/region tag queries.
 *
 * AUTHORITY MODEL
 *  Every mutator authority-guards at the TOP (via the sibling UEnt_EntityComponent) and early-returns
 *  on clients. Clients observe the set via the fast-array + OnTagChanged + the const tag-container view.
 *
 * The component exposes a const FGameplayTagContainer view so FGameplayTagQuery matching (used by the
 * query subsystem) works directly off the replicated state on both server and clients.
 */
UCLASS(ClassGroup = (DesignPatternsEntity), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Cooking, Collision))
class DESIGNPATTERNSENTITY_API UEnt_TagContainerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnt_TagContainerComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// ---- Mutators (AUTHORITY ONLY) -------------------------------------------------------------

	/**
	 * Add Tag (or add CountDelta to its stack). AUTHORITY ONLY. Creates the entry if absent.
	 * No-op on clients / for an invalid tag. Returns the new stack count.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Tag")
	int32 AddTag(FGameplayTag Tag, int32 CountDelta = 1);

	/**
	 * Remove CountDelta from Tag's stack; removes the entry when the count reaches zero (or always when
	 * CountDelta <= 0). AUTHORITY ONLY. Returns the remaining stack count (0 if removed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Tag")
	int32 RemoveTag(FGameplayTag Tag, int32 CountDelta = 1);

	/** Remove every tag. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Tag")
	void ClearTags();

	/** Authoritatively replace the entire tag set from a container (all counts set to 1). AUTHORITY ONLY. */
	void SetTags(const FGameplayTagContainer& InTags);

	// ---- Queries (safe on clients) -------------------------------------------------------------

	/** True if Tag (exact) is present. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Tag")
	bool HasTag(FGameplayTag Tag) const;

	/** True if Tag or a child of it is present (hierarchy match). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Tag")
	bool HasTagMatching(FGameplayTag Tag) const;

	/** Stack count for Tag (0 if absent). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Tag")
	int32 GetTagCount(FGameplayTag Tag) const;

	/** Append every explicit tag in the set into OutTags. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Tag")
	void GetExplicitTags(FGameplayTagContainer& OutTags) const;

	/** True if this entity's tags satisfy Query. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Tag")
	bool MatchesQuery(const FGameplayTagQuery& Query) const;

	/** Const view of the explicit tags, kept in sync with the replicated entries (for query matching). */
	const FGameplayTagContainer& GetTagContainerView() const { return CachedTagView; }

	/** This entity's stable id (read from the sibling entity component), or invalid. */
	FSeam_EntityId GetOwnEntityId() const;

	/** Fired (server and clients) when a tag is added/removed/changed. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Tag")
	FEnt_OnTagChanged OnTagChanged;

	/** Called by the fast-array entry callbacks on clients to surface a replicated change. */
	void HandleReplicatedTagChange(FGameplayTag ChangedTag);

protected:
	/** RepNotify: tag state arrived/changed on a client. */
	UFUNCTION()
	void OnRep_Tags();

private:
	/** The replicated tags. The only replicated state on this component. */
	UPROPERTY(ReplicatedUsing = OnRep_Tags)
	FEnt_ReplicatedTagArray Tags;

	/** Cached explicit-tag view rebuilt whenever the entries change (server + clients). */
	UPROPERTY(Transient)
	FGameplayTagContainer CachedTagView;

	/** Cached sibling entity component (resolved off the owner); non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnt_EntityComponent> CachedEntityComponent;

	/** True once registered with the world query subsystem. */
	bool bRegisteredWithQuery = false;

	/** True when this machine owns authority over the entity. */
	bool HasEntityAuthority() const;

	/** Resolve (and cache) the sibling entity component. */
	UEnt_EntityComponent* ResolveEntityComponent() const;

	/** Rebuild CachedTagView from the current entries. */
	void RebuildTagView();

	/** Register/unregister this component with the world tag-query subsystem. */
	void RegisterWithQuery();
	void UnregisterFromQuery();

	/** Broadcast the local "tags changed" bus message. */
	void BroadcastTagChanged(FGameplayTag ChangedTag) const;
};
