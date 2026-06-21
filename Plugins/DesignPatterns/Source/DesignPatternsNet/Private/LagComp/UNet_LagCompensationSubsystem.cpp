// Copyright DesignPatterns plugin. All Rights Reserved.

#include "LagComp/UNet_LagCompensationSubsystem.h"
#include "DesignPatternsNetNativeTags.h"
#include "Net/Seam_HitRewindTarget.h"
#include "Identity/Seam_TeamAffinity.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"

void UNet_LagCompensationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegisterSelfAsService();

	// Capture runs off a frame ticker (not actor tick) so it records even when no actor drives it. The
	// capture itself is authority-gated and paused-aware inside CaptureFrame.
	CaptureTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime) -> bool
		{
			TimeSinceLastCapture += DeltaTime;
			const float Period = 1.f / FMath::Max(10.f, CaptureFrequencyHz);
			if (TimeSinceLastCapture >= Period)
			{
				TimeSinceLastCapture = 0.f;
				CaptureFrame();
			}
			return true; // keep ticking
		}));

	UE_LOG(LogDP, Verbose, TEXT("UNet_LagCompensationSubsystem initialized (authority=%d)."), HasWorldAuthority() ? 1 : 0);
}

void UNet_LagCompensationSubsystem::Deinitialize()
{
	if (CaptureTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CaptureTickHandle);
		CaptureTickHandle.Reset();
	}
	Tracks.Reset();

	Super::Deinitialize();
}

bool UNet_LagCompensationSubsystem::HasWorldAuthority() const
{
	const UWorld* W = GetWorld();
	return W && W->GetNetMode() != NM_Client;
}

double UNet_LagCompensationSubsystem::ServerTimeSeconds() const
{
	const UWorld* W = GetWorld();
	return W ? W->GetTimeSeconds() : 0.0;
}

UDP_ServiceLocatorSubsystem* UNet_LagCompensationSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

void UNet_LagCompensationSubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(NetNativeTags::Service_Net_LagComp, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

TScriptInterface<ISeam_TeamAffinity> UNet_LagCompensationSubsystem::ResolveTeamAffinity() const
{
	// Friendly-fire policy lives on the GameMode team component/subsystem, resolved via the locator by a
	// well-known service tag the GameMode module registers. We resolve it weakly each call (it may be
	// absent in a free-for-all project, in which case no friendly-fire filtering is applied).
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		static const FGameplayTag TeamServiceTag = FGameplayTag::RequestGameplayTag(TEXT("DP.Service.GameMode.Team"), /*ErrorIfNotFound=*/false);
		if (TeamServiceTag.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(TeamServiceTag))
			{
				if (Provider->Implements<USeam_TeamAffinity>())
				{
					TScriptInterface<ISeam_TeamAffinity> Result;
					Result.SetObject(Provider);
					Result.SetInterface(Cast<ISeam_TeamAffinity>(Provider));
					return Result;
				}
			}
		}
	}
	return nullptr;
}

void UNet_LagCompensationSubsystem::RegisterTarget(UObject* Target)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	if (!Target || !Target->Implements<USeam_HitRewindTarget>())
	{
		UE_LOG(LogDP, Warning, TEXT("LagComp::RegisterTarget rejected a null / non-ISeam_HitRewindTarget object."));
		return;
	}

	const FSeam_EntityId Id = ISeam_HitRewindTarget::Execute_GetRewindEntityId(Target);
	if (!Id.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("LagComp::RegisterTarget: target %s returned an invalid entity id; not tracked."), *Target->GetName());
		return;
	}

	FNet_RewindTrack& Track = Tracks.FindOrAdd(Id);
	Track.Target = Target;
	Track.OwnerActor = Target->GetTypedOuter<AActor>();
	// Keep any existing samples (re-registration after a brief drop preserves history).

	UE_LOG(LogDP, Verbose, TEXT("LagComp: registered target %s (id=%s). Tracked=%d."),
		*Target->GetName(), *Id.ToString(), Tracks.Num());
}

void UNet_LagCompensationSubsystem::UnregisterTarget(UObject* Target)
{
	if (!HasWorldAuthority() || !Target || !Target->Implements<USeam_HitRewindTarget>())
	{
		return;
	}
	const FSeam_EntityId Id = ISeam_HitRewindTarget::Execute_GetRewindEntityId(Target);
	Tracks.Remove(Id);
}

void UNet_LagCompensationSubsystem::PruneDeadTracks()
{
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		if (!It->Value.Target.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void UNet_LagCompensationSubsystem::CaptureFrame()
{
	if (!HasWorldAuthority())
	{
		return;
	}
	const UWorld* W = GetWorld();
	if (!W || W->IsPaused())
	{
		// Don't record while paused; the time base would create false gaps.
		return;
	}

	const double Now = ServerTimeSeconds();
	const double WindowSecs = (double)FMath::Max(50, HistoryWindowMs) / 1000.0;

	PruneDeadTracks();

	for (auto& Pair : Tracks)
	{
		FNet_RewindTrack& Track = Pair.Value;
		UObject* TargetObj = Track.Target.Get();
		if (!TargetObj)
		{
			continue;
		}

		FBoxSphereBounds Bounds(ForceInit);
		if (!ISeam_HitRewindTarget::Execute_GetRewindBounds(TargetObj, Bounds))
		{
			continue; // target declined to provide bounds this frame
		}

		FNet_RewindSample Sample;
		Sample.TimeSeconds = Now;
		Sample.Bounds = Bounds;
		Track.Samples.Add(Sample);

		// Evict samples older than the window.
		while (Track.Samples.Num() > 1 && (Now - Track.Samples[0].TimeSeconds) > WindowSecs)
		{
			Track.Samples.RemoveAt(0, 1);
		}
	}
}

bool UNet_LagCompensationSubsystem::SampleBoundsAtTime(const FNet_RewindTrack& Track, double RewindTime, FBoxSphereBounds& OutBounds)
{
	const TArray<FNet_RewindSample>& S = Track.Samples;
	if (S.Num() == 0)
	{
		return false;
	}
	if (S.Num() == 1)
	{
		OutBounds = S[0].Bounds;
		return true;
	}

	// Clamp outside the ring.
	if (RewindTime <= S[0].TimeSeconds)
	{
		OutBounds = S[0].Bounds;
		return true;
	}
	if (RewindTime >= S.Last().TimeSeconds)
	{
		OutBounds = S.Last().Bounds;
		return true;
	}

	// Find the bracketing pair and lerp the box centre/extent and sphere radius.
	for (int32 i = S.Num() - 1; i >= 1; --i)
	{
		const FNet_RewindSample& B = S[i];
		const FNet_RewindSample& A = S[i - 1];
		if (RewindTime >= A.TimeSeconds && RewindTime <= B.TimeSeconds)
		{
			const double Span = B.TimeSeconds - A.TimeSeconds;
			const float Alpha = (Span > UE_DOUBLE_KINDA_SMALL_NUMBER)
				? (float)FMath::Clamp((RewindTime - A.TimeSeconds) / Span, 0.0, 1.0)
				: 1.f;

			const FVector Origin = FMath::Lerp(A.Bounds.Origin, B.Bounds.Origin, Alpha);
			const FVector Extent = FMath::Lerp(A.Bounds.BoxExtent, B.Bounds.BoxExtent, Alpha);
			const float Radius = FMath::Lerp(A.Bounds.SphereRadius, B.Bounds.SphereRadius, Alpha);
			OutBounds = FBoxSphereBounds(Origin, Extent, Radius);
			return true;
		}
	}

	OutBounds = S.Last().Bounds;
	return true;
}

bool UNet_LagCompensationSubsystem::SegmentIntersectsBounds(const FVector& Start, const FVector& End, const FBoxSphereBounds& Bounds,
	FVector& OutEntryPoint, float& OutEntryDistance)
{
	// Cheap broad-phase: reject if the segment's closest approach to the sphere centre exceeds the radius.
	const FVector Seg = End - Start;
	const float SegLenSq = (float)Seg.SizeSquared();
	{
		const FVector ToCentre = Bounds.Origin - Start;
		const float T = (SegLenSq > KINDA_SMALL_NUMBER) ? FMath::Clamp((float)FVector::DotProduct(ToCentre, Seg) / SegLenSq, 0.f, 1.f) : 0.f;
		const FVector Closest = Start + Seg * T;
		if (FVector::DistSquared(Closest, Bounds.Origin) > FMath::Square(Bounds.SphereRadius))
		{
			return false;
		}
	}

	// Narrow-phase: slab method against the AABB centred at Origin with BoxExtent.
	const FVector BoxMin = Bounds.Origin - Bounds.BoxExtent;
	const FVector BoxMax = Bounds.Origin + Bounds.BoxExtent;

	float TMin = 0.f;
	float TMax = 1.f;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float S = (float)Start[Axis];
		const float D = (float)Seg[Axis];
		const float MinA = (float)BoxMin[Axis];
		const float MaxA = (float)BoxMax[Axis];

		if (FMath::Abs(D) < KINDA_SMALL_NUMBER)
		{
			// Segment parallel to this slab: must already be inside it.
			if (S < MinA || S > MaxA)
			{
				return false;
			}
		}
		else
		{
			float T1 = (MinA - S) / D;
			float T2 = (MaxA - S) / D;
			if (T1 > T2) { Swap(T1, T2); }
			TMin = FMath::Max(TMin, T1);
			TMax = FMath::Min(TMax, T2);
			if (TMin > TMax)
			{
				return false;
			}
		}
	}

	OutEntryPoint = Start + Seg * TMin;
	OutEntryDistance = (float)(Seg.Size() * TMin);
	return true;
}

bool UNet_LagCompensationSubsystem::RewindAndValidate(AActor* Instigator, double ShooterTimestamp,
	const FVector& TraceStart, const FVector& TraceEnd, FNet_RewindResult& OutResult) const
{
	OutResult = FNet_RewindResult();

	if (!HasWorldAuthority())
	{
		return false;
	}

	const double Now = ServerTimeSeconds();
	const double MaxRewindSecs = (double)FMath::Max(50, MaxRewindMs) / 1000.0;
	// Clamp the requested timestamp into [now - MaxRewind, now] (anti-cheat: a client cannot rewind further
	// than the server allows, nor claim a future time).
	const double RewindTime = FMath::Clamp(ShooterTimestamp, Now - MaxRewindSecs, Now);
	OutResult.RewindTimeSeconds = RewindTime;

	const TScriptInterface<ISeam_TeamAffinity> Team = ResolveTeamAffinity();

	float BestDistance = TNumericLimits<float>::Max();
	for (const auto& Pair : Tracks)
	{
		const FNet_RewindTrack& Track = Pair.Value;
		UObject* TargetObj = Track.Target.Get();
		if (!TargetObj)
		{
			continue;
		}

		AActor* TargetActor = Track.OwnerActor.Get();

		// Never hit yourself.
		if (TargetActor && TargetActor == Instigator)
		{
			continue;
		}

		// Friendly-fire filter (if a team seam is available).
		if (Team.GetObject() && Instigator && TargetActor)
		{
			if (ISeam_TeamAffinity::Execute_AreFriendly(Team.GetObject(), Instigator, TargetActor))
			{
				continue;
			}
		}

		FBoxSphereBounds Bounds(ForceInit);
		if (!SampleBoundsAtTime(Track, RewindTime, Bounds))
		{
			continue;
		}

		FVector Entry;
		float Dist;
		if (SegmentIntersectsBounds(TraceStart, TraceEnd, Bounds, Entry, Dist))
		{
			if (Dist < BestDistance)
			{
				BestDistance = Dist;
				OutResult.bHit = true;
				OutResult.TargetId = Pair.Key;
				OutResult.ImpactPoint = Entry;
			}
		}
	}

	return OutResult.bHit;
}

bool UNet_LagCompensationSubsystem::ConfirmHitAtTime(AActor* Instigator, double ShooterTimestamp,
	const FVector& TraceStart, const FVector& TraceEnd,
	FGameplayTag DamageChannel, const FSeam_NetValue& Magnitude,
	FNet_RewindResult& OutResult)
{
	if (!HasWorldAuthority())
	{
		return false;
	}

	if (!RewindAndValidate(Instigator, ShooterTimestamp, TraceStart, TraceEnd, OutResult))
	{
		return false;
	}

	// Resolve the confirmed target's seam and apply the confirmed hit on the authority.
	if (const FNet_RewindTrack* Track = Tracks.Find(OutResult.TargetId))
	{
		if (UObject* TargetObj = Track->Target.Get())
		{
			ISeam_HitRewindTarget::Execute_ApplyConfirmedHit(TargetObj, Instigator, DamageChannel, Magnitude, NAME_None);

			UE_LOG(LogDP, Verbose, TEXT("LagComp: confirmed hit on id=%s at t=%.3f (instigator=%s)."),
				*OutResult.TargetId.ToString(), OutResult.RewindTimeSeconds,
				Instigator ? *Instigator->GetName() : TEXT("<none>"));
			return true;
		}
	}

	OutResult.bHit = false;
	return false;
}

FString UNet_LagCompensationSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalSamples = 0;
	for (const auto& Pair : Tracks)
	{
		TotalSamples += Pair.Value.Samples.Num();
	}
	return FString::Printf(TEXT("LagComp: %d targets, %d samples, window=%dms, maxRewind=%dms, auth=%d"),
		Tracks.Num(), TotalSamples, HistoryWindowMs, MaxRewindMs, HasWorldAuthority() ? 1 : 0);
}
