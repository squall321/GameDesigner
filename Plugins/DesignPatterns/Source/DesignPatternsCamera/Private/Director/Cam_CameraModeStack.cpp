// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Director/Cam_CameraModeStack.h"
#include "Core/DPLog.h"
#include "Curves/CurveFloat.h"

UCam_CameraModeStack::UCam_CameraModeStack()
{
}

FGuid UCam_CameraModeStack::PushMode(UCam_CameraMode* Mode, int32 Priority, const FCam_ViewContext& SeedContext)
{
	if (!Mode)
	{
		UE_LOG(LogDP, Warning, TEXT("[Camera] PushMode called with null mode; ignored."));
		return FGuid();
	}

	FCam_StackEntry Entry;
	Entry.RequestId = FGuid::NewGuid();
	Entry.Priority = Priority;
	Entry.SerialOrder = NextSerial++;
	Entry.Mode = Mode;
	Entry.TargetWeight = 0.f;   // recomputed in EvaluateStack based on which entry is top
	Entry.CurrentWeight = 0.f;

	Entries.Add(Entry);

	// Let the mode seed any transient smoothing state from the current view.
	Mode->OnEnterStack(SeedContext);

	UE_LOG(LogDP, Verbose, TEXT("[Camera] Pushed mode %s (priority %d, id %s); stack=%d"),
		*Mode->GetModeTag().ToString(), Priority, *Entry.RequestId.ToString(EGuidFormats::Short), Entries.Num());

	return Entry.RequestId;
}

void UCam_CameraModeStack::PopMode(FGuid RequestId)
{
	if (!RequestId.IsValid())
	{
		return;
	}
	const int32 Index = Entries.IndexOfByPredicate([&RequestId](const FCam_StackEntry& E)
	{
		return E.RequestId == RequestId;
	});
	if (Index == INDEX_NONE)
	{
		return;
	}
	if (Entries[Index].Mode)
	{
		Entries[Index].Mode->OnExitStack();
	}
	Entries.RemoveAt(Index);
	UE_LOG(LogDP, Verbose, TEXT("[Camera] Popped mode id %s; stack=%d"),
		*RequestId.ToString(EGuidFormats::Short), Entries.Num());
}

void UCam_CameraModeStack::ClearAll()
{
	for (FCam_StackEntry& Entry : Entries)
	{
		if (Entry.Mode)
		{
			Entry.Mode->OnExitStack();
		}
	}
	Entries.Reset();
}

int32 UCam_CameraModeStack::FindTopIndex() const
{
	int32 Best = INDEX_NONE;
	int32 BestPriority = MIN_int32;
	uint64 BestSerial = 0;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		const FCam_StackEntry& E = Entries[i];
		if (!E.IsValid())
		{
			continue;
		}
		if (E.Priority > BestPriority || (E.Priority == BestPriority && E.SerialOrder > BestSerial))
		{
			Best = i;
			BestPriority = E.Priority;
			BestSerial = E.SerialOrder;
		}
	}
	return Best;
}

float UCam_CameraModeStack::ShapeAlpha(float LinearAlpha) const
{
	const float A = FMath::Clamp(LinearAlpha, 0.f, 1.f);
	if (BlendCurve)
	{
		return FMath::Clamp(BlendCurve->GetFloatValue(A), 0.f, 1.f);
	}
	// Default ease: smoothstep (3a^2 - 2a^3).
	return A * A * (3.f - 2.f * A);
}

FCam_CameraView UCam_CameraModeStack::EvaluateStack(float DeltaTime, const FCam_ViewContext& Context, const FCam_CameraView& Fallback)
{
	if (Entries.Num() == 0)
	{
		return Fallback;
	}

	const int32 TopIndex = FindTopIndex();

	// Advance per-entry weights: the top mode eases toward 1, all others ease toward 0.
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		FCam_StackEntry& E = Entries[i];
		if (!E.IsValid())
		{
			continue;
		}
		const bool bIsTop = (i == TopIndex);
		E.TargetWeight = bIsTop ? 1.f : 0.f;

		const float BlendTime = bIsTop ? E.Mode->GetBlendInTime() : E.Mode->GetBlendOutTime();
		if (BlendTime <= 0.f || DeltaTime <= 0.f)
		{
			E.CurrentWeight = E.TargetWeight;
		}
		else
		{
			// Move toward target at a rate that fully blends over BlendTime seconds.
			const float Step = DeltaTime / BlendTime;
			E.CurrentWeight = FMath::FInterpConstantTo(E.CurrentWeight, E.TargetWeight, 1.f, Step);
		}
		E.Mode->SetBlendWeight(E.CurrentWeight);
	}

	// Prune fully-faded non-top entries so they stop being evaluated and can be GC'd when popped.
	// We only auto-prune entries that have been explicitly popped elsewhere; here we keep all pushed
	// entries (a popped entry is removed in PopMode). Non-top entries that reach ~0 simply contribute
	// nothing but remain available to re-blend if they become top again.

	// Evaluate every entry with weight > epsilon, in ascending order, and blend on top of the base.
	struct FEvaluated { float Weight; FCam_CameraView View; uint64 Serial; int32 Priority; };
	TArray<FEvaluated, TInlineAllocator<8>> Evaluated;
	for (const FCam_StackEntry& E : Entries)
	{
		if (!E.IsValid() || E.CurrentWeight <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		FCam_CameraView View;
		// Seed each mode's OutView from the previous camera view so partially-blended modes that read
		// nothing still produce sane output.
		View.Location = Context.PreviousCameraLocation;
		View.Rotation = Context.PreviousCameraRotation;
		View.FOV = Fallback.FOV;
		E.Mode->UpdateView(DeltaTime, Context, View);
		Evaluated.Add({ E.CurrentWeight, View, E.SerialOrder, E.Priority });
	}

	if (Evaluated.Num() == 0)
	{
		return Fallback;
	}

	// Order by (priority, serial) ascending so higher-priority/later modes blend last (on top).
	Evaluated.Sort([](const FEvaluated& A, const FEvaluated& B)
	{
		if (A.Priority != B.Priority) { return A.Priority < B.Priority; }
		return A.Serial < B.Serial;
	});

	// Start from the lowest entry's view, then layer each higher entry by its (curve-shaped) weight.
	FCam_CameraView Result = Evaluated[0].View;
	for (int32 i = 1; i < Evaluated.Num(); ++i)
	{
		const float Alpha = ShapeAlpha(Evaluated[i].Weight);
		Result = FCam_CameraView::Blend(Result, Evaluated[i].View, Alpha);
	}

	// If the top mode hasn't fully blended in, ease from the fallback (the live camera) toward Result
	// using the top entry's shaped weight so a freshly-pushed mode doesn't snap.
	if (TopIndex != INDEX_NONE)
	{
		const float TopAlpha = ShapeAlpha(Entries[TopIndex].CurrentWeight);
		Result = FCam_CameraView::Blend(Fallback, Result, TopAlpha);
	}

	return Result;
}

FGameplayTag UCam_CameraModeStack::GetTopModeTag() const
{
	const int32 TopIndex = FindTopIndex();
	if (TopIndex != INDEX_NONE && Entries[TopIndex].Mode)
	{
		return Entries[TopIndex].Mode->GetModeTag();
	}
	return FGameplayTag();
}

UCam_CameraMode* UCam_CameraModeStack::GetTopMode() const
{
	const int32 TopIndex = FindTopIndex();
	return (TopIndex != INDEX_NONE) ? Entries[TopIndex].Mode : nullptr;
}

FString UCam_CameraModeStack::BuildDebugString() const
{
	const int32 TopIndex = FindTopIndex();
	const FString TopTag = (TopIndex != INDEX_NONE && Entries[TopIndex].Mode)
		? Entries[TopIndex].Mode->GetModeTag().ToString()
		: TEXT("<none>");
	FString Detail;
	for (const FCam_StackEntry& E : Entries)
	{
		if (E.Mode)
		{
			Detail += FString::Printf(TEXT(" [%s p%d w%.2f]"),
				*E.Mode->GetModeTag().ToString(), E.Priority, E.CurrentWeight);
		}
	}
	return FString::Printf(TEXT("Top=%s Modes=%d%s"), *TopTag, Entries.Num(), *Detail);
}
