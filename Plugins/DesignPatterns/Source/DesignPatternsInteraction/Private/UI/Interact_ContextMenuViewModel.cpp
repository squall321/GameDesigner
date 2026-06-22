// Copyright DesignPatterns plugin. All Rights Reserved.

#include "UI/Interact_ContextMenuViewModel.h"

// Register the FieldNotify field ids declared in the header. These must match the FieldNotify-tagged getters.
UE_FIELD_NOTIFICATION_IMPLEMENTATION_BEGIN(UInteract_ContextMenuViewModel)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(UInteract_ContextMenuViewModel, Menu)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(UInteract_ContextMenuViewModel, SelectedIndex)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(UInteract_ContextMenuViewModel, VerbCount)
UE_FIELD_NOTIFICATION_IMPLEMENTATION_END(UInteract_ContextMenuViewModel)

void UInteract_ContextMenuViewModel::SetMenu(const FInteract_VerbMenu& InMenu)
{
	const int32 OldCount = Menu.Verbs.Num();

	// FInteract_VerbMenu has no operator!= (TArray member), so assign + broadcast unconditionally.
	Menu = InMenu;
	BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Menu);

	if (Menu.Verbs.Num() != OldCount)
	{
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::VerbCount);
	}

	// Reset the selection to the menu's default (clamped), broadcasting only if it changed.
	const int32 NewSelection = Menu.Verbs.IsValidIndex(Menu.DefaultVerbIndex) ? Menu.DefaultVerbIndex : INDEX_NONE;
	if (NewSelection != SelectedIndex)
	{
		SelectedIndex = NewSelection;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::SelectedIndex);
	}
}

void UInteract_ContextMenuViewModel::SetSelectedIndex(int32 Index)
{
	int32 Clamped = INDEX_NONE;
	if (Menu.Verbs.Num() > 0)
	{
		Clamped = FMath::Clamp(Index, 0, Menu.Verbs.Num() - 1);
	}

	if (Clamped != SelectedIndex)
	{
		SelectedIndex = Clamped;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::SelectedIndex);
	}
}

void UInteract_ContextMenuViewModel::StepSelection(int32 Delta)
{
	const int32 Count = Menu.Verbs.Num();
	if (Count == 0)
	{
		SetSelectedIndex(INDEX_NONE);
		return;
	}

	const int32 Start = (SelectedIndex == INDEX_NONE) ? 0 : SelectedIndex;
	// Wrap modulo the verb count (handles negative Delta correctly).
	const int32 Next = ((Start + Delta) % Count + Count) % Count;
	SetSelectedIndex(Next);
}

FInteract_VerbAvailability UInteract_ContextMenuViewModel::GetVerbAt(int32 Index) const
{
	if (Menu.Verbs.IsValidIndex(Index))
	{
		return Menu.Verbs[Index];
	}
	return FInteract_VerbAvailability();
}

FGameplayTag UInteract_ContextMenuViewModel::GetSelectedVerb() const
{
	if (Menu.Verbs.IsValidIndex(SelectedIndex))
	{
		return Menu.Verbs[SelectedIndex].Verb;
	}
	return FGameplayTag();
}

void UInteract_ContextMenuViewModel::CommitSelection()
{
	if (!Menu.Verbs.IsValidIndex(SelectedIndex))
	{
		return;
	}
	const FInteract_VerbAvailability& Entry = Menu.Verbs[SelectedIndex];
	if (!Entry.bEnabled || !Entry.Verb.IsValid())
	{
		// Committing a disabled verb is a no-op (the UI may instead show the reason text).
		return;
	}
	OnVerbChosen.Broadcast(Entry.Verb);
}
