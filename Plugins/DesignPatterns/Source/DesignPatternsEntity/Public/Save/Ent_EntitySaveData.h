// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Ent_EntitySaveData.generated.h"

/**
 * The durable per-trait save fragment captured from one live trait.
 *
 * The trait's own state record type is opaque to the save system, so it is carried as an
 * FInstancedStruct (local/save side only — never replicated). On restore the entity component
 * matches the fragment back to a live trait by TraitClassTag and hands TraitState to it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_TraitSaveFragment
{
	GENERATED_BODY()

	/** Trait-kind id (UEnt_Trait::TraitClassTag) this fragment was captured from / restores to. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Save")
	FGameplayTag TraitClassTag;

	/** The trait's own opaque save record. Local/save only — never replicated. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Save")
	FInstancedStruct TraitState;

	FEnt_TraitSaveFragment() = default;
};

/**
 * The complete durable save record for one entity, produced by UEnt_EntityComponent::CaptureState
 * and consumed by RestoreState.
 *
 * Holds the stable identity (so a load can re-key world/grid/agent state), the archetype tag (so a
 * loader can verify/rebuild the right trait set), and one fragment per live trait. This whole record
 * is wrapped in an FInstancedStruct by the ISeam_Persistable seam, so the save object never needs to
 * know the concrete type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntitySaveData
{
	GENERATED_BODY()

	/** The entity's net/save-stable id. Restored verbatim so cross-system keys stay valid. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Save")
	FSeam_EntityId EntityId;

	/** The entity's archetype identity tag. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Save")
	FGameplayTag ArchetypeTag;

	/** One captured fragment per live trait, keyed by TraitClassTag. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Save")
	TArray<FEnt_TraitSaveFragment> TraitFragments;

	FEnt_EntitySaveData() = default;

	/** Find the captured fragment for a given trait kind, or null. */
	const FEnt_TraitSaveFragment* FindFragment(const FGameplayTag& InTraitClassTag) const
	{
		for (const FEnt_TraitSaveFragment& Fragment : TraitFragments)
		{
			if (Fragment.TraitClassTag == InTraitClassTag)
			{
				return &Fragment;
			}
		}
		return nullptr;
	}
};
