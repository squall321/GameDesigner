// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mix/Audio_MixController.h"
#include "Mix/Audio_MixProfileDataAsset.h"
#include "Reverb/Audio_ReverbMixProfileDataAsset.h"
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
	// ADDITIVE: tear down any applied reverb-zone submix effects.
	ApplyExtraSubmixEffects(nullptr);
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
	const FAudio_ActiveMixSnapshot* Active = Stack.IsValidIndex(ActiveIndex) ? &Stack[ActiveIndex] : nullptr;
	ApplySnapshot(Active);

	// ADDITIVE: let subclasses add/remove submix-effect chains (reverb presets) for the new active
	// snapshot. Base implementation is a no-op, so shipped behaviour is unchanged.
	ApplyExtraSubmixEffects(Active);
}

// =====================================================================================================
// ADDITIVE deepening — dynamic mixing depth
// =====================================================================================================

FGuid UAudio_MixController::PushProfileBlended(UAudio_MixProfileDataAsset* Profile, float BlendTimeOverride, int32 PriorityOverride)
{
	if (!Profile)
	{
		UE_LOG(LogDP, Warning, TEXT("Audio_MixController::PushProfileBlended called with null profile."));
		return FGuid();
	}

	FAudio_ActiveMixSnapshot Snapshot;
	Snapshot.Handle = FGuid::NewGuid();
	Snapshot.Profile = Profile;
	Snapshot.Priority = (PriorityOverride >= 0) ? PriorityOverride : Profile->Priority;
	Snapshot.Sequence = NextSequence++;
	Snapshot.BlendTimeOverride = BlendTimeOverride; // < 0 => use per-override FadeTime

	Stack.Add(MoveTemp(Snapshot));
	const FGuid Result = Stack.Last().Handle;

	UE_LOG(LogDP, Verbose,
		TEXT("Audio_MixController: pushed BLENDED mix profile '%s' (priority %d, blend %.2fs, depth %d)."),
		*Profile->DataTag.ToString(), Stack.Last().Priority, BlendTimeOverride, Stack.Num());

	RefreshActive();
	return Result;
}

void UAudio_MixController::GetEffectiveDuckVolumes(TMap<FGameplayTag, float>& Out) const
{
	Out.Reset();
	if (!Stack.IsValidIndex(ActiveIndex) || !Stack[ActiveIndex].Profile)
	{
		return;
	}

	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	// Defensive fallback when the settings CDO is somehow unavailable: a moderate duck.
	const float DefaultDuck = Settings ? Settings->DefaultDuckVolume : 0.4f;

	for (const FAudio_DuckRule& Rule : Stack[ActiveIndex].Profile->DuckRules)
	{
		if (!Rule.TargetCategory.IsValid())
		{
			continue;
		}
		const float RuleDuck = (Rule.DuckVolume < 0.f) ? DefaultDuck : FMath::Clamp(Rule.DuckVolume, 0.f, 1.f);
		// If two rules target the same category keep the deepest (lowest) duck.
		float& Existing = Out.FindOrAdd(Rule.TargetCategory, 1.f);
		Existing = FMath::Min(Existing, RuleDuck);
	}
}

void UAudio_MixController::ApplyExtraSubmixEffects(const FAudio_ActiveMixSnapshot* Active)
{
	// Determine the reverb profile that SHOULD be applied for the now-active snapshot (if any).
	UAudio_ReverbMixProfileDataAsset* DesiredReverb = nullptr;
	float BlendTime = -1.f;
	if (Active && Active->Profile)
	{
		DesiredReverb = Cast<UAudio_ReverbMixProfileDataAsset>(Active->Profile);
		BlendTime = Active->BlendTimeOverride;
	}

	// Nothing changed: same reverb profile still active. Leave its effects in place.
	if (DesiredReverb == AppliedReverbProfile)
	{
		return;
	}

	// Remove the previously-applied reverb effects (active profile changed or cleared).
	if (AppliedReverbProfile)
	{
		AppliedReverbProfile->ApplySubmixEffects(this, /*bActive=*/false, /*BlendTimeOverride=*/-1.f);
		AppliedReverbProfile = nullptr;
	}

	// Apply the new reverb effects (if the new active profile is a reverb profile).
	if (DesiredReverb)
	{
		DesiredReverb->ApplySubmixEffects(this, /*bActive=*/true, BlendTime);
		AppliedReverbProfile = DesiredReverb;
	}
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
