// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Ent_TraitDefinition.generated.h"

/** How a second add of the same trait-kind is resolved on an entity. */
UENUM(BlueprintType)
enum class EEnt_TraitStackPolicy : uint8
{
	/** A second add replaces the first (the shipped UEnt_EntityComponent::AddTrait default). */
	Replace,

	/** A second add is ignored; the existing trait keeps its state. */
	Ignore,

	/** A second add increments a stack count carried on the advanced trait's replicated payload. */
	Stack
};

/**
 * Design-time metadata for a trait KIND, keyed by the trait's CapabilityTag.
 *
 * IMPORTANT — this is keyed off the REAL UEnt_Trait::CapabilityTag (its identity tag), NOT a
 * nonexistent "TraitClassTag". A trait subclass (UEnt_AdvancedTrait) softly references its definition
 * so the advanced/spawn layer can consult dependencies/conflicts/priority/stacking at add-time WITHOUT
 * modifying the shipped UEnt_Trait or UEnt_EntityComponent::AddTrait.
 *
 * Discoverable through the data registry (UDP_DataAsset by DataTag). Carries NO behaviour — purely the
 * policy a higher layer applies.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Entity Trait Definition"))
class DESIGNPATTERNSENTITY_API UEnt_TraitDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The trait-kind this definition describes (matches the live trait's UEnt_Trait::CapabilityTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition", meta = (Categories = "Ent.Trait,Ent.Cap"))
	FGameplayTag TraitCapabilityTag;

	/**
	 * Trait-kind tags this trait depends on — they must already be present (or be added together) for
	 * this trait to be valid on an entity. Checked by UEnt_AdvancedTrait at add/enable time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition", meta = (Categories = "Ent.Trait,Ent.Cap"))
	FGameplayTagContainer RequiredTraitTags;

	/**
	 * Trait-kind tags this trait conflicts with — the entity must not carry any of them. Checked by
	 * UEnt_AdvancedTrait at add/enable time (a conflict keeps the trait disabled).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition", meta = (Categories = "Ent.Trait,Ent.Cap"))
	FGameplayTagContainer ConflictingTraitTags;

	/**
	 * Apply priority — higher applies later, so a higher-priority trait's contribution wins when several
	 * derive the same value. A pure ordering tunable, not a hardcoded gameplay magnitude.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition")
	int32 ApplyPriority = 0;

	/** How a duplicate add of this trait-kind is resolved. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition")
	EEnt_TraitStackPolicy StackPolicy = EEnt_TraitStackPolicy::Replace;

	/** Maximum stack count when StackPolicy == Stack (clamped). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|TraitDefinition",
		meta = (ClampMin = "1", EditCondition = "StackPolicy == EEnt_TraitStackPolicy::Stack"))
	int32 MaxStackCount = 1;

	//~ Begin UDP_DataAsset.
	/** All trait definitions share one asset-manager bucket so the registry enumerates them together. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset.
};
