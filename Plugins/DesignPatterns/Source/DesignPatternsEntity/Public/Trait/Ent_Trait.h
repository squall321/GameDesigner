// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Capability/Ent_CapabilityProvider.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Ent_Trait.generated.h"

// The entity-core component (spine area) that owns and ticks traits. Forward-declared only — this header
// is the contract the spine consumes, so it must not hard-include the component.
class UEnt_EntityComponent;

/**
 * Composable unit of entity behaviour/data.
 *
 * A trait is an EditInlineNew, DefaultToInstanced UObject so each entity instance owns its own copy
 * (authored inline on an archetype asset, then instanced onto the live entity component). A trait can:
 *   - advertise one or more capabilities (CapabilityTag + ProvidedCapabilities) via IEnt_CapabilityProvider;
 *   - react to its lifecycle (OnTraitAdded / OnTraitTick / OnTraitRemoved);
 *   - serialize its own durable state (SaveState / RestoreState) as an FInstancedStruct.
 *
 * Traits hold NO replicated state themselves — they are subobjects of the owning component and are not
 * network-relevant on their own. Authoritative, replicated state must live on the owning component or a
 * dedicated AInfo/component carrier; a trait that needs replicated data should drive it through such a
 * carrier rather than declaring Replicated UPROPERTYs here.
 *
 * Abstract: ship a concrete subclass (or author a Blueprint trait) — UEnt_TagSetTrait and
 * UEnt_StatBagTrait are provided.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced,
	CollapseCategories, Category = "DesignPatterns|Entity|Trait")
class DESIGNPATTERNSENTITY_API UEnt_Trait : public UObject, public IEnt_CapabilityProvider
{
	GENERATED_BODY()

public:
	UEnt_Trait();

	/**
	 * Primary capability/kind id this trait represents (e.g. Ent.Trait.StatBag). Used as the trait's
	 * identity for query and for routing save records; also added to the provided-capability set so a
	 * consumer can find the trait object by this tag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|Trait", meta = (Categories = "Ent.Trait,Ent.Cap"))
	FGameplayTag CapabilityTag;

	/**
	 * Additional capability ids this trait advertises beyond CapabilityTag. The full provided set is
	 * CapabilityTag (if valid) plus these. Authored per archetype.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|Trait", meta = (Categories = "Ent.Cap"))
	FGameplayTagContainer ProvidedCapabilities;

	/**
	 * Whether this trait wants OnTraitTick called by the owning component. Authored per trait so the
	 * spine can skip ticking inert data-only traits (tunable, not a hardcoded gameplay number).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|Trait")
	bool bWantsTick = false;

	//~ Begin trait lifecycle (driven by the owning UEnt_EntityComponent on the game thread).

	/** Called once after the trait has been added to OwningComponent and registered. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Trait")
	void OnTraitAdded(UEnt_EntityComponent* OwningComponent);

	/** Called each tick (only when bWantsTick) with the frame delta seconds. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Trait")
	void OnTraitTick(float DeltaSeconds);

	/** Called once just before the trait is removed/destroyed; release any external references here. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Trait")
	void OnTraitRemoved();

	//~ End trait lifecycle.

	//~ Begin trait persistence (called by the entity's ISeam_Persistable participant on the game thread).

	/**
	 * Write this trait's durable state into Out as an FInstancedStruct. Default writes nothing
	 * (Out is reset to an empty/invalid struct). Override in stateful traits.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Trait")
	void SaveState(FInstancedStruct& Out) const;

	/**
	 * Restore previously-captured state from In. Default ignores In. Override in stateful traits.
	 * MUST tolerate an empty/invalid or mismatched struct (treat as "no saved state").
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Trait")
	void RestoreState(const FInstancedStruct& In);

	//~ End trait persistence.

	//~ Begin IEnt_CapabilityProvider — backed by CapabilityTag + ProvidedCapabilities.
	virtual void GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const override;
	virtual bool HasCapability_Implementation(FGameplayTag InCapabilityTag) const override;
	virtual UObject* GetCapabilityObject_Implementation(FGameplayTag InCapabilityTag) const override;
	//~ End IEnt_CapabilityProvider.

	/** Non-owning back-reference to the component that owns this trait; null until OnTraitAdded. */
	UEnt_EntityComponent* GetOwningComponent() const { return OwningComponent.Get(); }

	/** True if this trait advertises InCapabilityTag (CapabilityTag or in ProvidedCapabilities). */
	bool ProvidesCapability(FGameplayTag InCapabilityTag) const;

protected:
	/**
	 * Weak back-reference to the owning component — non-owning (the component owns the trait), so weak
	 * and null-checked before deref. Set during OnTraitAdded, cleared on removal.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnt_EntityComponent> OwningComponent;
};
