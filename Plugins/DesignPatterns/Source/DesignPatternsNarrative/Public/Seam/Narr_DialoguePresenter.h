// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Dialogue/Narr_DialogueTypes.h"
#include "Narr_DialoguePresenter.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UNarr_DialoguePresenter : public UInterface
{
	GENERATED_BODY()
};

/**
 * THE single local UI channel for dialogue presentation.
 *
 * A dialogue runner drives exactly one presenter (bound PER local-player runner via
 * UNarr_DialogueRunnerComponent::SetPresenter — NOT a global service-locator slot, because each split-
 * screen / local player must own its own on-screen dialogue UI). The runner calls ShowLine / ShowChoices
 * as it walks the graph and HideDialogue when the conversation ends; the presenter (a UMG widget, an HUD
 * view-model, or a test double) is responsible only for rendering and for routing the player's
 * advance/choice input BACK into the runner (AdvanceLine / SelectChoice).
 *
 * This is COSMETIC: presentation is local on each machine and never replicated. The interface is a
 * BlueprintNativeEvent surface so a project's UMG widget can implement it directly in Blueprint while a
 * C++ HUD view-model can implement the _Implementation overrides.
 */
class DESIGNPATTERNSNARRATIVE_API INarr_DialoguePresenter
{
	GENERATED_BODY()

public:
	/**
	 * Present a single line. The presenter should display Line.Text attributed to Line.Speaker and, if
	 * Line.AutoAdvanceSeconds <= 0, wait for the player to advance (calling the runner's AdvanceLine).
	 * When AutoAdvanceSeconds > 0 the RUNNER owns the timer and will advance itself; the presenter need
	 * only render and may show a countdown.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Narrative|Presenter")
	void ShowLine(const FNarr_DialogueLine& Line);

	/**
	 * Present a set of choices. The presenter renders one selectable entry per choice (a disabled choice,
	 * bEnabled == false, should be shown non-selectable or hidden per the project's UX) and, when the
	 * player picks one, routes that choice's ChoiceId back into the runner via SelectChoice.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Narrative|Presenter")
	void ShowChoices(const TArray<FNarr_DialogueChoice>& Choices);

	/**
	 * Tear down any on-screen dialogue UI. Called when the conversation ends (completed/dead-end/aborted)
	 * or when the runner is rebound to a different presenter.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Narrative|Presenter")
	void HideDialogue();
};
