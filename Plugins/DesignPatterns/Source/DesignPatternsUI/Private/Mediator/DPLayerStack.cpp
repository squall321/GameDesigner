// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mediator/DPLayerStack.h"
#include "View/DPViewBase.h"
#include "Core/DPLog.h"
#include "Blueprint/UserWidget.h"

void UDP_LayerStack::InitLayer(FGameplayTag InLayerTag)
{
	LayerTag = InLayerTag;
}

bool UDP_LayerStack::Push(FGameplayTag ScreenTag, UDP_ViewBase* Widget, int32 ZOrder)
{
	if (!Widget)
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] LayerStack '%s' Push called with null widget for screen '%s'."),
			*LayerTag.ToString(), *ScreenTag.ToString());
		return false;
	}

	Widget->AddToViewport(ZOrder);

	FDP_LayerStackEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.ScreenTag = ScreenTag;
	Entry.Widget = Widget;
	Entry.ZOrder = ZOrder;

	UE_LOG(LogDP, Verbose, TEXT("[UI] Layer '%s' pushed screen '%s' (depth %d)."),
		*LayerTag.ToString(), *ScreenTag.ToString(), Entries.Num());
	return true;
}

bool UDP_LayerStack::Pop()
{
	if (Entries.Num() == 0)
	{
		return false;
	}

	const FDP_LayerStackEntry Entry = Entries.Pop();
	RemoveEntryWidget(Entry);

	UE_LOG(LogDP, Verbose, TEXT("[UI] Layer '%s' popped screen '%s' (depth %d)."),
		*LayerTag.ToString(), *Entry.ScreenTag.ToString(), Entries.Num());
	return true;
}

bool UDP_LayerStack::PopToTag(FGameplayTag ScreenTag)
{
	// Find the top-most entry matching the tag.
	int32 TargetIndex = INDEX_NONE;
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		if (Entries[Index].ScreenTag == ScreenTag)
		{
			TargetIndex = Index;
			break;
		}
	}

	if (TargetIndex == INDEX_NONE)
	{
		return false;
	}

	// Pop everything from the top down to and including TargetIndex.
	for (int32 Index = Entries.Num() - 1; Index >= TargetIndex; --Index)
	{
		RemoveEntryWidget(Entries[Index]);
		Entries.RemoveAt(Index);
	}

	UE_LOG(LogDP, Verbose, TEXT("[UI] Layer '%s' popped to/including screen '%s' (depth %d)."),
		*LayerTag.ToString(), *ScreenTag.ToString(), Entries.Num());
	return true;
}

void UDP_LayerStack::Clear()
{
	for (const FDP_LayerStackEntry& Entry : Entries)
	{
		RemoveEntryWidget(Entry);
	}
	Entries.Reset();
}

void UDP_LayerStack::RemoveEntryWidget(const FDP_LayerStackEntry& Entry)
{
	if (Entry.Widget)
	{
		// Drop the ViewModel binding before removing so the view tears down deterministically.
		if (Entry.Widget->IsInViewport())
		{
			Entry.Widget->RemoveFromParent();
		}
	}
}

UDP_ViewBase* UDP_LayerStack::GetTopWidget() const
{
	return Entries.Num() > 0 ? Entries.Last().Widget : nullptr;
}

FGameplayTag UDP_LayerStack::GetTopScreenTag() const
{
	return Entries.Num() > 0 ? Entries.Last().ScreenTag : FGameplayTag();
}

bool UDP_LayerStack::ContainsScreen(FGameplayTag ScreenTag) const
{
	return Entries.ContainsByPredicate([&ScreenTag](const FDP_LayerStackEntry& Entry)
	{
		return Entry.ScreenTag == ScreenTag;
	});
}

void UDP_LayerStack::DumpTo(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("  Layer '%s' (%d widget(s)):"),
		*LayerTag.ToString(), Entries.Num()));

	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		const FDP_LayerStackEntry& Entry = Entries[Index];
		const TCHAR* Marker = (Index == Entries.Num() - 1) ? TEXT("* ") : TEXT("  ");
		OutLines.Add(FString::Printf(TEXT("    %s[%d] %s (Z=%d) -> %s"),
			Marker, Index, *Entry.ScreenTag.ToString(), Entry.ZOrder,
			Entry.Widget ? *Entry.Widget->GetName() : TEXT("<null>")));
	}
}
