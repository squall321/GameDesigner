// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Trait/Ent_Trait.h"
#include "Ent_TagSetTrait.generated.h"

/**
 * Durable save record for UEnt_TagSetTrait — the live tag set as a plain container.
 *
 * Carried inside an FInstancedStruct, so it is never a plain replicated UPROPERTY; the entity's
 * ISeam_Persistable participant routes it through the save object.
 */
USTRUCT()
struct DESIGNPATTERNSENTITY_API FEnt_TagSetTraitSave
{
	GENERATED_BODY()

	/** The runtime tag set at capture time. */
	UPROPERTY()
	FGameplayTagContainer Tags;
};

/**
 * Concrete data trait holding a runtime-mutable set of gameplay tags on the entity.
 *
 * Use it for cheap entity flags/markers (e.g. Ent.State.* style runtime tags) without standing up a
 * dedicated component. The set is seeded from InitialTags when the trait is added, then mutated at
 * runtime via the Add/Remove API. The whole set is captured/restored through SaveState/RestoreState.
 *
 * This is data-only state on a (non-replicated) subobject — callers needing the set to be authoritative
 * across the network must mirror it onto a replicated component; this trait is the single-machine model.
 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Entity Tag-Set Trait"))
class DESIGNPATTERNSENTITY_API UEnt_TagSetTrait : public UEnt_Trait
{
	GENERATED_BODY()

public:
	UEnt_TagSetTrait();

	/** Tags the live set is seeded with when this trait is added to an entity. Authored per archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|TagSet")
	FGameplayTagContainer InitialTags;

	/** Add a tag to the live set. Returns true if it was newly added. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|TagSet")
	bool AddTag(FGameplayTag Tag);

	/** Remove a tag from the live set. Returns true if it was present and removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|TagSet")
	bool RemoveTag(FGameplayTag Tag);

	/** Exact-match test against the live set. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|TagSet")
	bool HasTag(FGameplayTag Tag) const;

	/** Hierarchy-aware test against the live set (Tag or any of its parents/children, engine semantics). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|TagSet")
	bool HasTagMatching(FGameplayTag Tag) const;

	/** Copy of the current live set. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|TagSet")
	FGameplayTagContainer GetTags() const { return LiveTags; }

	/** Replace the entire live set. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|TagSet")
	void SetTags(const FGameplayTagContainer& NewTags);

	//~ Begin UEnt_Trait.
	virtual void OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In) override;
	virtual void SaveState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	//~ End UEnt_Trait.

protected:
	/**
	 * The runtime-mutable tag set. Transient so it is not serialized as a UPROPERTY on the archetype
	 * (authoring uses InitialTags); durable persistence goes through SaveState/RestoreState instead.
	 */
	UPROPERTY(Transient)
	FGameplayTagContainer LiveTags;
};
