// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Interact_VerbDefinition.generated.h"

/**
 * Data-driven definition of an interaction verb (e.g. Open, Pick Up, Talk, Use).
 *
 * Identity is the inherited DataTag (a child of DP.Data.Interact.Verb), resolved through the core
 * data registry — interactables and prompts reference verbs by tag, never by hard pointer. This
 * subclass deliberately does NOT override GetDataAssetType(), so it forms its own asset-manager
 * bucket named after the class.
 *
 * Holds presentation (display name) and activation policy (instant vs hold), plus input metadata so
 * the same verb can be bound to an input action / icon by the UI layer without code changes.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSINTERACTION_API UInteract_VerbDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UInteract_VerbDefinition();

	/** Player-facing action label, e.g. "Open", "Pick Up". Shown on the interaction prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb")
	FText VerbDisplayName;

	/**
	 * When true the verb requires the player to hold the input for HoldSeconds before it activates;
	 * when false it activates instantly on press.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb")
	bool bHoldToActivate = false;

	/** How long (seconds) the input must be held when bHoldToActivate is true. Ignored otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb",
		meta = (EditCondition = "bHoldToActivate", ClampMin = "0.0", Units = "s"))
	float HoldSeconds = 1.5f;

	/**
	 * Identity tag of the input action bound to this verb (a project-defined tag, e.g.
	 * Input.Action.Interact). The input layer maps this to a concrete UInputAction; kept as a tag so
	 * this module never hard-depends on EnhancedInput.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Input")
	FGameplayTag InputActionTag;

	/** Optional icon/style identity tag for the prompt UI (resolved by the UI layer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Input")
	FGameplayTag PromptStyleTag;
};
