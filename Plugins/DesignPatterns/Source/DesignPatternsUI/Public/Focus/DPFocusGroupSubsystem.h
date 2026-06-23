// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "Focus/DPFocusTypes.h"
#include "DPFocusGroupSubsystem.generated.h"

class UWidget;

/**
 * Per-local-player gamepad focus depth manager.
 *
 * It provides three things a controller-friendly UI needs and that raw Slate focus does not give
 * you out of the box:
 *  - TAG-KEYED FOCUS GROUPS: register a set of widgets under a tag and focus the group (restoring
 *    the member that last had focus within it);
 *  - A MODAL FOCUS TRAP STACK: push a trap so navigation is confined to one group while a modal is
 *    open (cooperating with the UI module's modal layer concept), and pop it to RESTORE the widget
 *    that was focused before the modal opened;
 *  - FOCUS RESTORE on pop.
 *
 * It WRAPS FSlateApplication::SetUserFocus / widget SetFocus rather than reinventing focus, and is
 * scoped per local player so split-screen players navigate independently. Members are held weakly,
 * so the subsystem never keeps a widget alive and prunes dead members on use.
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_FocusGroupSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Register (or replace) the ordered member list for GroupTag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Focus")
	void RegisterFocusGroup(FGameplayTag GroupTag, const TArray<UWidget*>& Members);

	/** Remove a registered group. Does not affect the trap stack unless the group is trapped. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Focus")
	void UnregisterFocusGroup(FGameplayTag GroupTag);

	/**
	 * Push a modal focus trap for GroupTag, remembering the currently-focused widget. While this
	 * trap is on top, FocusGroup confines focus to the group's members. Optionally focus
	 * InitialFocus immediately (else the group's first/last-focused member).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Focus", meta = (AdvancedDisplay = "InitialFocus"))
	void PushFocusTrap(FGameplayTag GroupTag, UWidget* InitialFocus = nullptr);

	/** Pop the top focus trap and restore the widget that was focused before it was pushed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Focus")
	void PopFocusTrap();

	/** Focus a group (its last-focused member, or first member). No-op for an unknown/empty group. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Focus")
	void FocusGroup(FGameplayTag GroupTag);

	/** True if GroupTag is currently the top trap on the stack. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Focus")
	bool IsGroupTrapped(FGameplayTag GroupTag) const;

	/** The widget that currently holds user focus for this local player, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Focus")
	UWidget* GetCurrentFocus() const;

	/** Depth of the modal trap stack (0 = no modal trap active). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Focus")
	int32 GetTrapDepth() const { return TrapStack.Num(); }

private:
	/** Set user focus to Widget through Slate, using this local player's user index. Returns success. */
	bool SetFocusToWidget(UWidget* Widget);

	/** The slate user index for this subsystem's local player (best-effort; 0 fallback). */
	int32 GetUserIndex() const;

	/** Focus the member at MemberIndex of Group (clamped/pruned), updating LastFocusedIndex. */
	void FocusMember(FDP_FocusGroup& Group, int32 MemberIndex);

	/** Registered focus groups keyed by tag. */
	UPROPERTY()
	TMap<FGameplayTag, FDP_FocusGroup> Groups;

	/** Modal focus-trap stack (top = last). */
	UPROPERTY()
	TArray<FDP_FocusTrapEntry> TrapStack;
};
