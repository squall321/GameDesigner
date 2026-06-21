// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quest/RPG_QuestDefinition.h"
#include "Quest/RPG_QuestGraphTypes.h"
#include "RPG_QuestGraphDefinition.generated.h"

/**
 * Branching quest content: a URPG_QuestDefinition extended with stages, branch outcomes, accept gates and
 * a whole-quest time limit.
 *
 * ADDITIVE: it inherits Title / Summary / Objectives unchanged. The inherited flat Objectives array stays
 * meaningful as a LINEAR FALLBACK — a project that never assigns a Tracker component still gets the base
 * quest log's auto-complete behaviour. When Stages is non-empty the URPG_ObjectiveTrackerComponent drives
 * the graph instead, activating only the current stage's objectives into the log.
 *
 * It deliberately does NOT override GetDataAssetType, so every quest (linear or branching) shares the one
 * "RPG_Quest" asset-manager bucket and resolves by DataTag exactly as before; save data keyed by quest tag
 * stays stable whether a quest is upgraded from linear to branching.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_QuestGraphDefinition : public URPG_QuestDefinition
{
	GENERATED_BODY()

public:
	/** The stage the graph begins at when the quest is accepted. Must name a stage in Stages. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest|Graph", meta = (Categories = "RPG.Quest.Stage"))
	FGameplayTag StartStage;

	/** All stages of this quest, keyed by StageTag (see FindStage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest|Graph")
	TArray<FRPG_QuestStage> Stages;

	/**
	 * Gates that must ALL pass before the quest may be accepted (offered). Evaluated by the tracker's
	 * ActivateQuestGraph; an empty list means the quest is always acceptable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest|Graph")
	TArray<FRPG_StageGate> AcceptGates;

	/**
	 * Seconds the player has to finish the WHOLE quest from acceptance; 0 = no overall limit. Anchored to
	 * the saved acceptance time so it survives reload. Independent of per-stage TimeLimitSeconds (whichever
	 * elapses first applies).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Quest|Graph", meta = (ClampMin = "0.0"))
	float QuestTimeLimitSeconds = 0.f;

	/** @return the stage with StageTag, or null. O(n) over Stages (graphs are small, scanned rarely). */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	const FRPG_QuestStage* FindStage(FGameplayTag StageTag) const;

	/** @return the designated start stage, or null when StartStage is unset / dangling. */
	const FRPG_QuestStage* GetStartStage() const;

	/** @return true if this graph has at least one stage (otherwise it behaves as a linear quest). */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	bool IsBranching() const { return Stages.Num() > 0; }

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Validates a present/valid StartStage, unique stage tags, and that branch NextStage targets exist. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
