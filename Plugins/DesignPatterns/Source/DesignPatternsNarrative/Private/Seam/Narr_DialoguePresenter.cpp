// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/Narr_DialoguePresenter.h"

// INERT native defaults for the dialogue presenter seam. A project that binds no presenter leaves
// these unoverridden, so the dialogue runner's calls become safe no-ops. The runner already logs a
// warning when no presenter is bound, so these stubs need not log again. A project's UMG widget or
// HUD view-model overrides these in Blueprint or C++ to implement the real presentation UI.

void INarr_DialoguePresenter::ShowLine_Implementation(const FNarr_DialogueLine& /*Line*/)
{
	// Inert default: absence of presenter is safe (logged in runner).
}

void INarr_DialoguePresenter::ShowChoices_Implementation(const TArray<FNarr_DialogueChoice>& /*Choices*/)
{
	// Inert default: absence of presenter is safe (logged in runner).
}

void INarr_DialoguePresenter::HideDialogue_Implementation()
{
	// Inert default: absence of presenter is safe.
}
