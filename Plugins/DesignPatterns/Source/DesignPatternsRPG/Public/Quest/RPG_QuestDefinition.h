// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_QuestDefinition.generated.h"

/**
 * One objective within a quest: a tag-identified counter target.
 *
 * Progress is tracked at runtime by the quest log as a per-objective counter; the objective
 * is complete when its counter reaches RequiredCount.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestObjective
{
	GENERATED_BODY()

	/** Identity of this objective within the quest (e.g. "RPG.Objective.KillWolves"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest")
	FGameplayTag ObjectiveTag;

	/** Player-facing objective description. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest")
	FText Description;

	/** Counter value required to complete this objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest", meta = (ClampMin = "1"))
	int32 RequiredCount = 1;
};

/**
 * Tag-identified quest definition.
 *
 * Reuses the core data asset (UDP_DataAsset) so quest identity is the inherited DataTag
 * (e.g. "RPG.Quest.WolfCull") and the asset is resolvable, load-free, through the core data
 * registry. The runtime quest log references quests purely by DataTag, so save data stays
 * stable across catalog changes.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_QuestDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Player-facing quest title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest")
	FText Title;

	/** Player-facing quest summary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest", meta = (MultiLine = "true"))
	FText Summary;

	/** Ordered objectives; the quest is complete when all are satisfied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest")
	TArray<FRPG_QuestObjective> Objectives;

	//~ Begin UDP_DataAsset
	/** Collapse every quest definition into one shared "RPG_Quest" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
