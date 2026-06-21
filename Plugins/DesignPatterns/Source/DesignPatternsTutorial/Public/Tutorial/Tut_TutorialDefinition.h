// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"

#include "Tut_TutorialDefinition.generated.h"

class UTut_Condition;

/**
 * One step of a tutorial sequence.
 *
 * A step surfaces an instruction when its Trigger condition is met, optionally highlights a UI element and
 * gates input while active, and advances to the next step when its Completion condition is met. Trigger and
 * Completion are INLINE-authored UTut_Condition policy objects (EditInlineNew), so designers compose steps
 * entirely in the editor with no code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSTUTORIAL_API FTut_TutorialStep
{
	GENERATED_BODY()

	/**
	 * Condition that must be met before this step's instruction is surfaced. When null the step triggers
	 * immediately on entry (the common case for the first step). Composed inline in the editor.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Tutorial|Step")
	TObjectPtr<UTut_Condition> Trigger;

	/**
	 * Condition that, once met, advances past this step. When null the step is informational and the runner
	 * advances on the next explicit AdvanceStep call / the next surfaced trigger. Composed inline.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Tutorial|Step")
	TObjectPtr<UTut_Condition> Completion;

	/** The instruction text shown to the player while this step is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Step", meta = (MultiLine = true))
	FText Instruction;

	/**
	 * UI target to highlight while this step is active, via ISeam_UIHighlight. Empty disables highlighting.
	 * The target identity is resolved by the UI implementation (e.g. a HUD slot / inventory cell tag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Step", meta = (Categories = "DP.UI"))
	FGameplayTag HighlightTargetTag;

	/** Highlight style passed to ISeam_UIHighlight alongside the target (e.g. pulse / outline). Optional. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Step", meta = (Categories = "DP.UI"))
	FGameplayTag HighlightStyleTag;

	/** When true the runner pushes InputModeTag through ISeam_InputModeArbiter while this step is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Step")
	bool bGateInput = false;

	/**
	 * Input mode pushed through ISeam_InputModeArbiter while this step is active (only when bGateInput).
	 * Empty falls back to the developer-settings default tutorial input mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial|Step",
		meta = (EditCondition = "bGateInput", Categories = "DP.Input.Mode"))
	FGameplayTag InputModeTag;

	FTut_TutorialStep() = default;
};

/**
 * A complete, ordered tutorial sequence — a tag-identified UDP_DataAsset resolved through the core data
 * registry (UTut_TutorialSubsystem::StartTutorial takes the DataTag and loads this asset).
 *
 * Holds only data: an ordered list of FTut_TutorialStep. The runner subsystem owns all behaviour (condition
 * evaluation, highlighting, input gating, persistence). Authoring a tutorial is therefore pure content.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tutorial Definition"))
class DESIGNPATTERNSTUTORIAL_API UTut_TutorialDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The ordered steps of this tutorial. Steps run front-to-back; an empty list completes immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	TArray<FTut_TutorialStep> Steps;

	/**
	 * When true the runner does not gate input even if individual steps request it (a master override for a
	 * non-blocking, hint-style tutorial). Defaults false. Tunable per asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tutorial")
	bool bNeverGateInput = false;

	//~ Begin UDP_DataAsset
	/** Groups all tutorial definitions under one asset-manager type bucket for cooking/streaming. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on empty step lists and steps with neither instruction nor a completion condition. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
