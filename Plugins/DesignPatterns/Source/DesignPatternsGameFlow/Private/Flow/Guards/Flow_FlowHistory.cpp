// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Guards/Flow_FlowHistory.h"

void UFlow_FlowHistory::SetMaxDepth(int32 InMaxDepth)
{
	MaxDepth = FMath::Max(1, InMaxDepth);
	// Trim if the new bound is smaller than the current stack.
	while (PhaseStack.Num() > MaxDepth)
	{
		PhaseStack.RemoveAt(0);
	}
}

void UFlow_FlowHistory::PushPhase(FGameplayTag Phase)
{
	if (!Phase.IsValid())
	{
		return;
	}

	// Ignore a repeated push of the current top (idempotent transition).
	if (PhaseStack.Num() > 0 && PhaseStack.Last() == Phase)
	{
		return;
	}

	PhaseStack.Add(Phase);

	// Drop the oldest entries beyond the bound.
	while (PhaseStack.Num() > MaxDepth)
	{
		PhaseStack.RemoveAt(0);
	}
}

FGameplayTag UFlow_FlowHistory::PeekBackTarget() const
{
	// The back target is the entry directly below the top.
	if (PhaseStack.Num() >= 2)
	{
		return PhaseStack[PhaseStack.Num() - 2];
	}
	return FGameplayTag();
}

FGameplayTag UFlow_FlowHistory::PopForBack()
{
	if (PhaseStack.Num() >= 2)
	{
		// Remove the current top; the new top is the back target.
		PhaseStack.Pop();
		return PhaseStack.Last();
	}
	return FGameplayTag();
}

bool UFlow_FlowHistory::CanGoBack() const
{
	return PhaseStack.Num() >= 2;
}

void UFlow_FlowHistory::GetHistory(TArray<FGameplayTag>& OutHistory) const
{
	OutHistory = PhaseStack;
}

void UFlow_FlowHistory::Reset()
{
	PhaseStack.Reset();
}

bool UFlow_FlowHistory::BeginTransition()
{
	if (bInTransition)
	{
		return false;
	}
	bInTransition = true;
	return true;
}

void UFlow_FlowHistory::EndTransition()
{
	bInTransition = false;
}
