// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Focus/DPFocusGroupSubsystem.h"
#include "Core/DPLog.h"

#include "Components/Widget.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWidget.h"

void UDP_FocusGroupSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDP_FocusGroupSubsystem::Deinitialize()
{
	Groups.Reset();
	TrapStack.Reset();
	Super::Deinitialize();
}

int32 UDP_FocusGroupSubsystem::GetUserIndex() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	// ControllerId maps to the Slate user index for split-screen / multiple-gamepad setups.
	return (LP && LP->GetControllerId() >= 0) ? LP->GetControllerId() : 0;
}

void UDP_FocusGroupSubsystem::RegisterFocusGroup(FGameplayTag GroupTag, const TArray<UWidget*>& Members)
{
	if (!GroupTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Focus] RegisterFocusGroup with an invalid tag."));
		return;
	}

	FDP_FocusGroup& Group = Groups.FindOrAdd(GroupTag);
	Group.GroupTag = GroupTag;
	Group.Members.Reset();
	for (UWidget* Member : Members)
	{
		if (Member)
		{
			Group.Members.Add(Member);
		}
	}
	Group.LastFocusedIndex = 0;
}

void UDP_FocusGroupSubsystem::UnregisterFocusGroup(FGameplayTag GroupTag)
{
	Groups.Remove(GroupTag);
}

void UDP_FocusGroupSubsystem::PushFocusTrap(FGameplayTag GroupTag, UWidget* InitialFocus)
{
	if (!GroupTag.IsValid())
	{
		return;
	}

	FDP_FocusTrapEntry Entry;
	Entry.GroupTag = GroupTag;
	Entry.PreviousFocus = GetCurrentFocus();
	TrapStack.Add(Entry);

	if (InitialFocus)
	{
		SetFocusToWidget(InitialFocus);
		// Remember which member this is so a later FocusGroup restores it.
		if (FDP_FocusGroup* Group = Groups.Find(GroupTag))
		{
			const int32 Index = Group->Members.IndexOfByPredicate(
				[InitialFocus](const TWeakObjectPtr<UWidget>& W) { return W.Get() == InitialFocus; });
			if (Index != INDEX_NONE)
			{
				Group->LastFocusedIndex = Index;
			}
		}
	}
	else
	{
		FocusGroup(GroupTag);
	}
}

void UDP_FocusGroupSubsystem::PopFocusTrap()
{
	if (TrapStack.Num() == 0)
	{
		return;
	}

	const FDP_FocusTrapEntry Entry = TrapStack.Pop(/*bAllowShrinking*/ false);

	// Restore focus to whatever held it before the trap was pushed (if still alive).
	if (UWidget* Previous = Entry.PreviousFocus.Get())
	{
		SetFocusToWidget(Previous);
	}
	else if (TrapStack.Num() > 0)
	{
		// The previous focus is gone but there is still a trap below — focus its group.
		FocusGroup(TrapStack.Last().GroupTag);
	}
}

void UDP_FocusGroupSubsystem::FocusGroup(FGameplayTag GroupTag)
{
	// If a modal trap is active, only the trapped group may be focused.
	if (TrapStack.Num() > 0 && TrapStack.Last().GroupTag != GroupTag)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Focus] FocusGroup(%s) ignored: trapped to %s."),
			*GroupTag.ToString(), *TrapStack.Last().GroupTag.ToString());
		return;
	}

	FDP_FocusGroup* Group = Groups.Find(GroupTag);
	if (!Group || Group->Members.Num() == 0)
	{
		return;
	}

	FocusMember(*Group, Group->LastFocusedIndex);
}

void UDP_FocusGroupSubsystem::FocusMember(FDP_FocusGroup& Group, int32 MemberIndex)
{
	// Prune dead members first so indices stay meaningful.
	Group.Members.RemoveAll([](const TWeakObjectPtr<UWidget>& W) { return !W.IsValid(); });
	if (Group.Members.Num() == 0)
	{
		return;
	}

	const int32 ClampedIndex = FMath::Clamp(MemberIndex, 0, Group.Members.Num() - 1);
	if (UWidget* Member = Group.Members[ClampedIndex].Get())
	{
		if (SetFocusToWidget(Member))
		{
			Group.LastFocusedIndex = ClampedIndex;
		}
	}
}

bool UDP_FocusGroupSubsystem::SetFocusToWidget(UWidget* Widget)
{
	if (!Widget)
	{
		return false;
	}

	const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
	if (!Slate.IsValid())
	{
		// Not yet constructed in Slate — defer to the UMG focus path which handles this gracefully.
		Widget->SetFocus();
		return true;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocus(GetUserIndex(), Slate, EFocusCause::SetDirectly);
		return true;
	}

	// Headless / no Slate (server) — fall back to the UMG path.
	Widget->SetFocus();
	return true;
}

UWidget* UDP_FocusGroupSubsystem::GetCurrentFocus() const
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(GetUserIndex());
	if (!Focused.IsValid())
	{
		return nullptr;
	}

	// Map the focused SWidget back to a UWidget by scanning registered group members (the set of
	// widgets this subsystem actually manages). This avoids a fragile global UMG reverse-lookup.
	for (const TPair<FGameplayTag, FDP_FocusGroup>& Pair : Groups)
	{
		for (const TWeakObjectPtr<UWidget>& WeakMember : Pair.Value.Members)
		{
			if (UWidget* Member = WeakMember.Get())
			{
				if (Member->GetCachedWidget() == Focused)
				{
					return Member;
				}
			}
		}
	}
	return nullptr;
}

bool UDP_FocusGroupSubsystem::IsGroupTrapped(FGameplayTag GroupTag) const
{
	return TrapStack.Num() > 0 && TrapStack.Last().GroupTag == GroupTag;
}
