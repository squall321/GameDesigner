// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Ent_ArchetypeAsset.generated.h"

class UEnt_Trait;

/**
 * Data-driven definition of an entity KIND.
 *
 * An archetype is a UDP_DataAsset (so it is tag-identified via DataTag and discoverable through the data
 * registry) that authors the default trait composition for a kind of entity. The spine area's entity
 * component instantiates DefaultTraits onto a live entity at spawn, then adds the resolved capabilities to
 * its provider set.
 *
 * Archetypes support single-parent inheritance via ParentArchetype: the effective trait list and declared
 * capabilities are the parent chain's contributions followed by this asset's own (child overrides/extends
 * parent). The chain is walked defensively against cycles.
 *
 * DeclaredCapabilities is the design-time PROMISE of what entities of this kind expose; it is validated
 * against the union the traits actually provide so authoring mistakes surface in the editor.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Entity Archetype"))
class DESIGNPATTERNSENTITY_API UEnt_ArchetypeAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Traits authored on this archetype. Instanced subobjects (EditInlineNew) so each archetype owns its
	 * own trait instances; the entity component duplicates these onto each live entity.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|Entity|Archetype")
	TArray<TObjectPtr<UEnt_Trait>> DefaultTraits;

	/**
	 * Optional parent archetype this one extends. Soft reference so deep inheritance graphs do not force
	 * the whole tree to load. A child contributes its traits/capabilities AFTER the parent's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|Archetype")
	TSoftObjectPtr<UEnt_ArchetypeAsset> ParentArchetype;

	/**
	 * Capabilities this archetype declares it provides (the design-time contract). Validated against the
	 * union the resolved traits actually advertise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|Archetype", meta = (Categories = "Ent.Cap"))
	FGameplayTagContainer DeclaredCapabilities;

	/**
	 * Resolve the full inherited trait list (parent chain first, then this asset). This synchronously
	 * loads ParentArchetype if needed. The returned traits are the SOURCE templates — the caller (entity
	 * component) must DuplicateObject them under the live owner before use; do not mutate them in place.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|Archetype")
	void GetResolvedDefaultTraits(TArray<UEnt_Trait*>& OutTraits) const;

	/** Union of DeclaredCapabilities across the inherited chain (parent chain + this asset). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|Archetype")
	void GetResolvedDeclaredCapabilities(FGameplayTagContainer& OutCapabilities) const;

	/** Union of the capabilities the resolved traits actually advertise (what entities really expose). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|Archetype")
	void GetEffectiveProvidedCapabilities(FGameplayTagContainer& OutCapabilities) const;

	//~ Begin UDP_DataAsset.
	/** All entity archetypes share one asset-manager bucket so the registry can enumerate them together. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset.

#if WITH_EDITOR
	//~ Begin UObject (editor validation).
	/** Flags parent cycles, null traits, and DeclaredCapabilities the traits do not actually provide. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject.
#endif

private:
	/**
	 * Walk the parent chain depth-first into Out, guarding against cycles with Visited. Appends parents
	 * before self so children extend/override. Returns false if a cycle was detected.
	 */
	bool CollectChain(TArray<const UEnt_ArchetypeAsset*>& OutChain, TSet<const UEnt_ArchetypeAsset*>& Visited) const;
};
