// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

// Seams implemented by this component.
#include "Identity/Seam_EntityId.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Persist/Seam_Persistable.h"

// Entity-core area contract (sibling areas in this module): the capability seam (this component
// aggregates trait capabilities) and the trait fast-array (this area).
#include "Capability/Ent_CapabilityProvider.h"
#include "Entity/Ent_TraitArray.h"

#include "Ent_EntityComponent.generated.h"

class UEnt_Trait;
class UEnt_ArchetypeAsset;

/**
 * Broadcast (server and clients) after the entity's trait set or identity changes — e.g. a trait was
 * added/removed, the archetype was (re)applied, or replicated trait state arrived on a client.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnt_OnEntityChanged, UEnt_EntityComponent*, EntityComponent);

/**
 * The entity-trait spine: gives any actor a stable, net/save-stable identity and a composable,
 * server-authoritative set of traits whose capabilities are queryable through the capability seam.
 *
 * RESPONSIBILITIES
 *  - Identity: replicates a stable FSeam_EntityId (assigned exactly once, on authority) plus an
 *    archetype tag, and implements ISeam_EntityIdentity so World/Grid/Agents key off it without a
 *    hard dependency on this concrete type.
 *  - Traits: owns live UEnt_Trait instanced subobjects (an owning UPROPERTY array), duplicated from the
 *    archetype's resolved templates on BeginPlay (authority). Each live trait also gets a delta-replicated
 *    FEnt_TraitEntry so clients can mirror the trait set and answer capability queries.
 *  - Capabilities: implements IEnt_CapabilityProvider, aggregating each trait's advertised capabilities.
 *  - Persistence: implements ISeam_Persistable, capturing/restoring an FEnt_EntitySaveData that bundles
 *    identity + each trait's own opaque SaveState fragment.
 *  - Ticking: only ticks when at least one live trait wants it (UEnt_Trait::bWantsTick), driving
 *    OnTraitTick on those traits.
 *
 * AUTHORITY MODEL
 *  Every mutator (SetEntityId, AddTrait*, RemoveTrait, ApplyArchetype, RestoreState) guards authority
 *  at the top and early-returns on clients. Clients never construct or mutate the replicated trait set;
 *  they observe it through the fast-array and OnEntityChanged. This component is NEVER itself a
 *  replicated subsystem — it is the authoritative replicated carrier for entity state.
 */
UCLASS(ClassGroup = (DesignPatternsEntity), meta = (BlueprintSpawnableComponent),
	HideCategories = (Activation, Cooking, Collision))
class DESIGNPATTERNSENTITY_API UEnt_EntityComponent
	: public UActorComponent
	, public IEnt_CapabilityProvider
	, public ISeam_EntityIdentity
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	UEnt_EntityComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	// ---- Identity (ISeam_EntityIdentity) -------------------------------------------------------

	//~ Begin ISeam_EntityIdentity
	virtual FSeam_EntityId GetEntityId_Implementation() const override { return EntityId; }
	virtual FGameplayTag GetArchetypeTag_Implementation() const override { return ArchetypeTag; }
	//~ End ISeam_EntityIdentity

	/**
	 * Assign this entity's stable id. AUTHORITY ONLY, and only honoured while the id is still unset
	 * (an entity's id never changes once assigned). Pass an invalid id to request a freshly-generated
	 * one. No-op on clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Identity")
	void SetEntityId(FSeam_EntityId InId);

	/** Set the archetype identity tag. AUTHORITY ONLY. Does NOT itself apply traits — call ApplyArchetype. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Identity")
	void SetArchetypeTag(FGameplayTag InArchetypeTag);

	// ---- Archetype / trait construction --------------------------------------------------------

	/**
	 * The archetype asset that seeds this entity's trait set. Assigned in the editor or at spawn.
	 * On BeginPlay (authority) its resolved trait templates (parent chain included) are duplicated onto
	 * this component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|Archetype")
	TObjectPtr<UEnt_ArchetypeAsset> Archetype = nullptr;

	/**
	 * Instantiate the resolved traits of Asset (its full ParentArchetype chain, parents first) onto this
	 * entity, set the archetype tag from the (leaf) asset, and rebuild the replicated trait entries.
	 * AUTHORITY ONLY. Existing live traits are cleared first. No-op on clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Archetype")
	void ApplyArchetype(UEnt_ArchetypeAsset* Asset);

	// ---- Trait mutators (AUTHORITY ONLY) -------------------------------------------------------

	/**
	 * Add a trait by duplicating Template under this component (instanced ownership). AUTHORITY ONLY.
	 * The duplicate is initialized (OnTraitAdded), registered as a live trait and given a replicated
	 * entry. If a trait with the same CapabilityTag already exists it is replaced. No-op on clients.
	 * Returns the live trait (or null on failure / non-authority).
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Trait")
	UEnt_Trait* AddTrait(UEnt_Trait* Template);

	/**
	 * Construct and add a trait of TraitClass. AUTHORITY ONLY. Convenience over AddTrait that does the
	 * NewObject(this) for you. No-op on clients. Returns the live trait (or null).
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Trait", meta = (DeterminesOutputType = "TraitClass"))
	UEnt_Trait* AddTraitByClass(TSubclassOf<UEnt_Trait> TraitClass);

	/**
	 * Remove the live trait whose CapabilityTag matches TraitTag. AUTHORITY ONLY. Tears down the trait
	 * (OnTraitRemoved), drops it from the live array and removes its replicated entry. No-op on clients.
	 * Returns true if a trait was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Trait")
	bool RemoveTrait(FGameplayTag TraitTag);

	// ---- Trait / capability queries (safe on clients) ------------------------------------------

	/** Live trait whose CapabilityTag matches TraitTag, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait")
	UEnt_Trait* FindTraitByTag(FGameplayTag TraitTag) const;

	/** Typed accessor: the first live trait that is (or derives from) T, or null. */
	template <typename T>
	T* GetTrait() const
	{
		for (const TObjectPtr<UEnt_Trait>& Trait : LiveTraits)
		{
			if (T* Typed = Cast<T>(Trait))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/** Typed accessor by class (Blueprint-facing). Returns the first live trait of TraitClass, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait", meta = (DeterminesOutputType = "TraitClass"))
	UEnt_Trait* GetTraitByClass(TSubclassOf<UEnt_Trait> TraitClass) const;

	/** Read-only snapshot of the live trait objects (safe on clients). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait")
	TArray<UEnt_Trait*> GetAllTraits() const;

	//~ Begin IEnt_CapabilityProvider (aggregates the live traits' capabilities)
	virtual void GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const override;
	virtual bool HasCapability_Implementation(FGameplayTag CapabilityTag) const override;
	virtual UObject* GetCapabilityObject_Implementation(FGameplayTag CapabilityTag) const override;
	//~ End IEnt_CapabilityProvider

	// ---- Persistence (ISeam_Persistable) -------------------------------------------------------

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	// ---- Change notification -------------------------------------------------------------------

	/** Fired (server and clients) after the trait set or identity changes. */
	UPROPERTY(BlueprintAssignable, Category = "Entity|Trait")
	FEnt_OnEntityChanged OnEntityChanged;

	/** Called by the trait fast-array entry callbacks on clients to surface a replicated change. */
	void HandleReplicatedTraitChange();

	// ---- Additive: per-trait replicated scalar payload -----------------------------------------

	/**
	 * AUTHORITY ONLY. Write a trait's single net-relevant scalar into its replicated FEnt_TraitEntry
	 * (StatePayload), so subclasses such as UEnt_AdvancedTrait can mirror runtime enable/stack state to
	 * clients without a new replicated array. No-op on clients or for an unknown trait kind.
	 *
	 * This is purely additive: the shipped SyncReplicatedEntryForTrait leaves StatePayload unset, and
	 * this method only sets it for traits that opt in. Marks the entry dirty so the change replicates.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Trait")
	void SetTraitStatePayload(FGameplayTag TraitTag, FSeam_NetValue Payload);

	/** Read a trait's replicated StatePayload (works on clients), or an unset value if absent. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Trait")
	FSeam_NetValue GetTraitStatePayload(FGameplayTag TraitTag) const;

	// ---- Authority helper ----------------------------------------------------------------------

	/** True when this machine owns authority over the entity (server / standalone). */
	bool HasEntityAuthority() const;

	/** One-line debug string (entity id, archetype, trait count) for gameplay-debugger output. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Entity|Debug")
	FString GetEntityDebugString() const;

protected:
	/** RepNotify: a fresh entity id arrived on a client. */
	UFUNCTION()
	void OnRep_EntityId();

	/** RepNotify: the archetype tag arrived/changed on a client. */
	UFUNCTION()
	void OnRep_ArchetypeTag();

private:
	/**
	 * The stable, net/save-stable identity. Assigned exactly once on authority (in BeginPlay if still
	 * unset) and replicated; clients receive it via OnRep_EntityId and register with the registry then.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_EntityId, SaveGame, VisibleAnywhere, Category = "Entity|Identity")
	FSeam_EntityId EntityId;

	/** The archetype identity tag (what kind of thing this is). Replicated and saved. */
	UPROPERTY(ReplicatedUsing = OnRep_ArchetypeTag, SaveGame, VisibleAnywhere, Category = "Entity|Identity")
	FGameplayTag ArchetypeTag;

	/** Delta-replicated trait entries — the client-visible mirror of the live trait set. */
	UPROPERTY(Replicated)
	FEnt_TraitArray ReplicatedTraits;

	/**
	 * The live trait subobjects. OWNING references (instanced UObjects outered to this component), held
	 * as UPROPERTY TObjectPtr<> in a UPROPERTY array. Authoritatively built from the archetype; on
	 * clients, rebuilt to mirror the replicated entries when their classes can be resolved.
	 */
	UPROPERTY(Instanced, VisibleAnywhere, Category = "Entity|Trait")
	TArray<TObjectPtr<UEnt_Trait>> LiveTraits;

	/** True once BeginPlay has run, so registry/trait rebuild only happens after the world is ready. */
	bool bHasBegunPlay = false;

	/** Whether we have already registered (so we register exactly once across BeginPlay/OnRep ordering). */
	bool bRegistered = false;

	/** Build or refresh the replicated FEnt_TraitEntry for a single live trait (authority only). */
	void SyncReplicatedEntryForTrait(UEnt_Trait* Trait);

	/** Rebuild the entire replicated entry array from the current live traits (authority only). */
	void RebuildAllReplicatedEntries();

	/** Initialize a freshly-added/duplicated trait (call its OnTraitAdded hook), then update tick state. */
	void InitializeTrait(UEnt_Trait* Trait);

	/** Tear down a trait being removed (call its OnTraitRemoved hook). */
	void UninitializeTrait(UEnt_Trait* Trait);

	/**
	 * Client-side: reconcile the live trait objects to match the replicated entries. Adds missing
	 * traits (by class resolved from the archetype's resolved template list) and removes extras.
	 */
	void ReconcileLiveTraitsFromReplication();

	/** Recompute whether the component needs to tick (any live trait with bWantsTick). */
	void UpdateTickEnabled();

	/** Register this entity with the world entity registry (from BeginPlay / OnRep once id valid). */
	void RegisterWithRegistry();

	/** The capability tag a trait identifies itself by (its CapabilityTag). Empty for a null trait. */
	static FGameplayTag GetTraitIdentityTag(const UEnt_Trait* Trait);
};
