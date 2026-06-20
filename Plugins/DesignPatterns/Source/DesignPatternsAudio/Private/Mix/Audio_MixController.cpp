// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mix/Audio_MixController.h"
#include "Mix/Audio_MixProfileDataAsset.h"
#include "Settings/Audio_DeveloperSettings.h"
#include "Core/DPLog.h"

#include "Sound/SoundSubmix.h"
#include "Kismet/GameplayStatics.h"

FGuid UAudio_MixController::PushProfile(UAudio_MixProfileDataAsset* Profile, int32 PriorityOverride)
{
	if (!Profile)
	{
		UE_LOG(LogDP, Warning, TEXT("Audio_MixController::PushProfile called with null profile."));
		return FGuid();
	}

	FAudio_ActiveMixSnapshot Snapshot;
	Snapshot.Handle = FGuid::NewGuid();
	Snapshot.Profile = Profile;
	Snapshot.Priority = (PriorityOverride >= 0) ? PriorityOverride : Profile->Priority;
	Snapshot.Sequence = NextSequence++;

	Stack.Add(MoveTemp(Snapshot));
	const FGuid Result = Stack.Last().Handle;

	UE_LOG(LogDP, Verbose, TEXT("Audio_MixController: pushed mix profile '%s' (priority %d, depth %d)."),
		*Profile->DataTag.ToString(), Stack.Last().Priority, Stack.Num());

	RefreshActive();
	return Result;
}

void UAudio_MixController::PopProfile(const FGuid& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	const int32 RemovedIndex = Stack.IndexOfByPredicate(
		[&Handle](const FAudio_ActiveMixSnapshot& S) { return S.Handle == Handle; });

	if (RemovedIndex == INDEX_NONE)
	{
		return; // Already popped or never ours — safe no-op.
	}

	Stack.RemoveAt(RemovedIndex);
	RefreshActive();
}

void UAudio_MixController::ClearAll()
{
	Stack.Reset();
	ActiveIndex = INDEX_NONE;
	// Restore every submix we ever drove back to unity.
	ApplySnapshot(nullptr);
}

FGameplayTag UAudio_MixController::GetActiveProfileTag() const
{
	if (Stack.IsValidIndex(ActiveIndex) && Stack[ActiveIndex].Profile)
	{
		return Stack[ActiveIndex].Profile->DataTag;
	}
	return FGameplayTag();
}

float UAudio_MixController::GetActiveDuckVolume(const FGameplayTag& Category) const
{
	if (!Category.IsValid() || !Stack.IsValidIndex(ActiveIndex) || !Stack[ActiveIndex].Profile)
	{
		return 1.f;
	}

	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	// Defensive fallback when the settings CDO is somehow unavailable: a moderate duck so audio still
	// behaves sensibly rather than silencing or ignoring the duck entirely.
	const float DefaultDuck = Settings ? Settings->DefaultDuckVolume : 0.4f;

	float Deepest = 1.f;
	for (const FAudio_DuckRule& Rule : Stack[ActiveIndex].Profile->DuckRules)
	{
		// A rule applies if the requested category is the rule's target or a child of it.
		if (Rule.TargetCategory.IsValid() && Category.MatchesTag(Rule.TargetCategory))
		{
			const float RuleDuck = (Rule.DuckVolume < 0.f) ? DefaultDuck : FMath::Clamp(Rule.DuckVolume, 0.f, 1.f);
			Deepest = FMath::Min(Deepest, RuleDuck);
		}
	}
	return Deepest;
}

FString UAudio_MixController::GetDebugString() const
{
	const FGameplayTag Active = GetActiveProfileTag();
	return FString::Printf(TEXT("Mix[depth=%d active=%s]"),
		Stack.Num(), Active.IsValid() ? *Active.ToString() : TEXT("none"));
}

void UAudio_MixController::RefreshActive()
{
	// Choose the highest-priority snapshot; break ties by the later push sequence.
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Stack.Num(); ++Index)
	{
		if (BestIndex == INDEX_NONE)
		{
			BestIndex = Index;
			continue;
		}
		const FAudio_ActiveMixSnapshot& Best = Stack[BestIndex];
		const FAudio_ActiveMixSnapshot& Cand = Stack[Index];
		if (Cand.Priority > Best.Priority ||
			(Cand.Priority == Best.Priority && Cand.Sequence > Best.Sequence))
		{
			BestIndex = Index;
		}
	}

	ActiveIndex = BestIndex;
	ApplySnapshot(Stack.IsValidIndex(ActiveIndex) ? &Stack[ActiveIndex] : nullptr);
}

void UAudio_MixController::ApplySnapshot(const FAudio_ActiveMixSnapshot* Snapshot)
{
	// Build the set of submixes the new active profile drives.
	TArray<TObjectPtr<USoundSubmix>> NewApplied;
	if (Snapshot && Snapshot->Profile)
	{
		for (const FAudio_SubmixOverride& Override : Snapshot->Profile->SubmixOverrides)
		{
			// Sync-load is acceptable here: profiles are few and a push is a deliberate, infrequent
			// mix transition (combat start, pause). Soft refs keep the unloaded cost at zero.
			USoundSubmix* Submix = Override.Submix.LoadSynchronous();
			if (!Submix)
			{
				continue;
			}
			// SetSubmixOutputVolume is a safe no-op without an audio device (headless), so guarding
			// only the world-context object is enough.
			UGameplayStatics::SetSubmixOutputVolume(this, Submix, Override.Volume);
			NewApplied.Add(Submix);
		}
	}

	// Restore any submix that was driven by the previous active profile but is NOT in the new set.
	for (const TObjectPtr<USoundSubmix>& Prev : AppliedSubmixes)
	{
		if (Prev && !NewApplied.Contains(Prev))
		{
			UGameplayStatics::SetSubmixOutputVolume(this, Prev, 1.f);
		}
	}

	AppliedSubmixes = MoveTemp(NewApplied);
}
