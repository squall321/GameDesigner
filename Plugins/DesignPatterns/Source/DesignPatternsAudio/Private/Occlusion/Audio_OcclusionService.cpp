// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Occlusion/Audio_OcclusionService.h"
#include "Occlusion/Audio_OcclusionComponent.h"
#include "Occlusion/Audio_OcclusionSettings.h"

#include "Core/DPLog.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "CollisionQueryParams.h"

bool UAudio_OcclusionService::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// No point tracing occlusion with no audio device.
	return !IsRunningDedicatedServer();
}

void UAudio_OcclusionService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAudio_OcclusionService::Tick));

	UE_LOG(LogDP, Log, TEXT("Audio_OcclusionService initialized."));
}

void UAudio_OcclusionService::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Sources.Reset();

	UE_LOG(LogDP, Log, TEXT("Audio_OcclusionService deinitialized."));
	Super::Deinitialize();
}

void UAudio_OcclusionService::RegisterSource(UAudio_OcclusionComponent* Source)
{
	if (!Source)
	{
		return;
	}
	for (const TWeakObjectPtr<UAudio_OcclusionComponent>& Existing : Sources)
	{
		if (Existing.Get() == Source)
		{
			return;
		}
	}
	Sources.Add(Source);
}

void UAudio_OcclusionService::UnregisterSource(UAudio_OcclusionComponent* Source)
{
	Sources.RemoveAll([Source](const TWeakObjectPtr<UAudio_OcclusionComponent>& S) { return S.Get() == Source; });
}

bool UAudio_OcclusionService::Tick(float DeltaTime)
{
	const UAudio_OcclusionSettings* Settings = UAudio_OcclusionSettings::Get();
	if (!Settings || !Settings->bEnableOcclusion)
	{
		return true; // Disabled globally; keep the ticker but do nothing.
	}

	// Defensive fallback sweep interval if a config somehow drove it non-positive.
	const float Interval = FMath::Max(0.02f, Settings->SweepInterval);
	SweepAccumulator += DeltaTime;
	if (SweepAccumulator < Interval)
	{
		return true;
	}
	SweepAccumulator = 0.f;

	PruneSources();
	if (Sources.Num() == 0)
	{
		return true;
	}

	TArray<FVector> ListenerLocations;
	GatherListenerLocations(ListenerLocations);
	if (ListenerLocations.Num() == 0)
	{
		return true; // No listener yet (very early frame); nothing to trace against.
	}

	// Round-robin a budgeted batch so a scene with hundreds of emitters never traces them all at once.
	const int32 Budget = FMath::Clamp(Settings->MaxSourcesPerSweep, 1, Sources.Num());
	for (int32 Tested = 0; Tested < Budget; ++Tested)
	{
		if (Sources.Num() == 0)
		{
			break;
		}
		SweepCursor %= Sources.Num();
		UAudio_OcclusionComponent* Source = Sources[SweepCursor].Get();
		++SweepCursor;

		if (Source && Source->HasLiveVoices())
		{
			EvaluateSource(Source, ListenerLocations);
		}
	}

	return true; // keep ticking
}

void UAudio_OcclusionService::EvaluateSource(UAudio_OcclusionComponent* Source, const TArray<FVector>& ListenerLocations)
{
	UWorld* World = GetWorld();
	if (!World || !Source)
	{
		return;
	}

	const UAudio_OcclusionSettings* Settings = UAudio_OcclusionSettings::Get();
	const ECollisionChannel Channel = Settings ? Settings->OcclusionTraceChannel.GetValue() : ECC_Visibility;
	const float MaxDist = Settings ? Settings->MaxTraceDistance : 6000.f;

	const FVector SourceLoc = Source->GetSourceWorldLocation();

	// Find the NEAREST listener — the one most likely to hear this source — for splitscreen correctness.
	int32 NearestIndex = INDEX_NONE;
	float NearestDistSq = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < ListenerLocations.Num(); ++Index)
	{
		const float DSq = FVector::DistSquared(ListenerLocations[Index], SourceLoc);
		if (DSq < NearestDistSq)
		{
			NearestDistSq = DSq;
			NearestIndex = Index;
		}
	}
	if (NearestIndex == INDEX_NONE)
	{
		return;
	}

	// Distance cull: too far to matter -> treat as fully open (and skip the trace cost).
	if (MaxDist > 0.f && NearestDistSq > FMath::Square(MaxDist))
	{
		Source->SetTargetOcclusion(0.f);
		return;
	}

	const FVector ListenerLoc = ListenerLocations[NearestIndex];

	FCollisionQueryParams Q(SCENE_QUERY_STAT(DP_AudioOcclusion), /*bTraceComplex=*/false);
	if (const AActor* Owner = Source->GetOwner())
	{
		Q.AddIgnoredActor(Owner); // Don't self-occlude on the emitter's own collision.
	}

	const bool bBlocked = World->LineTraceTestByChannel(ListenerLoc, SourceLoc, Channel, Q);

	// Binary block -> full/zero target; the component eases between them so the transition is smooth.
	Source->SetTargetOcclusion(bBlocked ? 1.f : 0.f);
}

void UAudio_OcclusionService::GatherListenerLocations(TArray<FVector>& OutLocations) const
{
	OutLocations.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (FAudioDeviceHandle DeviceHandle = World->GetAudioDevice())
	{
		if (FAudioDevice* Device = DeviceHandle.GetAudioDevice())
		{
			const TArray<FListener>& Listeners = Device->GetListeners();
			OutLocations.Reserve(Listeners.Num());
			for (const FListener& L : Listeners)
			{
				OutLocations.Add(L.Transform.GetLocation());
			}
		}
	}
}

void UAudio_OcclusionService::PruneSources()
{
	Sources.RemoveAll([](const TWeakObjectPtr<UAudio_OcclusionComponent>& S) { return !S.IsValid(); });
	if (Sources.Num() > 0)
	{
		SweepCursor %= Sources.Num();
	}
	else
	{
		SweepCursor = 0;
	}
}

FString UAudio_OcclusionService::GetDPDebugString_Implementation() const
{
	int32 LiveSources = 0;
	for (const TWeakObjectPtr<UAudio_OcclusionComponent>& S : Sources)
	{
		if (S.IsValid())
		{
			++LiveSources;
		}
	}
	return FString::Printf(TEXT("Occlusion[sources=%d cursor=%d]"), LiveSources, SweepCursor);
}
