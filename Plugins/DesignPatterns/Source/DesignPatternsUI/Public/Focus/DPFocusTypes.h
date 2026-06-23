// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DPFocusTypes.generated.h"

class UWidget;

/**
 * A named, tag-keyed group of focusable widgets for gamepad navigation.
 *
 * Members are stored weakly: a group never keeps a widget alive, and dead members are skipped when
 * the group is focused. The group order is the navigation order used by FocusGroup().
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_FocusGroup
{
	GENERATED_BODY()

	/** Identity of the group (e.g. DP.UI.FocusGroup.MainMenu). */
	UPROPERTY()
	FGameplayTag GroupTag;

	/** Ordered, weakly-held members. Dead entries are pruned on use. */
	UPROPERTY()
	TArray<TWeakObjectPtr<UWidget>> Members;

	/** Index of the member that last held focus in this group (for restore within the group). */
	UPROPERTY()
	int32 LastFocusedIndex = 0;
};

/**
 * One entry on the modal focus-trap stack: the trapped group plus the widget that held focus when
 * the trap was pushed, so PopFocusTrap can restore it.
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_FocusTrapEntry
{
	GENERATED_BODY()

	/** The group whose members focus is confined to while this trap is on top. */
	UPROPERTY()
	FGameplayTag GroupTag;

	/** The widget that held focus before the trap was pushed; restored on pop. */
	UPROPERTY()
	TWeakObjectPtr<UWidget> PreviousFocus;
};
