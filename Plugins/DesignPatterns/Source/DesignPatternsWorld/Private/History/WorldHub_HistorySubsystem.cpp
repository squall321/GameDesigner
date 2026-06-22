// Copyright DesignPatterns plugin. All Rights Reserved.

#include "History/WorldHub_HistorySubsystem.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "WorldHub_NativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "Stats/Stats.h"

//~ USubsystem lifecycle -----------------------------------------------------------------------

void UWorldHub_HistorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Bind the hub's change delegate so change-driven capture knows when state moved.
	ResolveHub();

	RegisterSelfAsService(/*bRegister=*/true);

	UE_LOG(LogDP, Log, TEXT("[WorldHub] History subsystem initialized (authority=%s, maxFrames=%d)."),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"), MaxFrames);
}

void UWorldHub_HistorySubsystem::Deinitialize()
{
	// Unbind from the hub so no dangling delegate outlives this subsystem.
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		H->OnValueChanged.RemoveAll(this);
	}

	RegisterSelfAsService(/*bRegister=*/false);

	Frames.Reset();
	Hub.Reset();
	Clock.Reset();
	CachedClockObject.Reset();

	Super::Deinitialize();
}

bool UWorldHub_HistorySubsystem::HasWorldAuthority() const
{
	const UWorld* W = GetWorld();
	return W && W->GetNetMode() != NM_Client;
}

//~ Resolution helpers -------------------------------------------------------------------------

UWorldHub_StateHubSubsystem* UWorldHub_HistorySubsystem::ResolveHub()
{
	if (UWorldHub_StateHubSubsystem* Cached = Hub.Get())
	{
		return Cached;
	}

	UWorldHub_StateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (Resolved)
	{
		Hub = Resolved;
		// Bind change-driven capture exactly once (AddUnique tolerates repeated resolves).
		Resolved->OnValueChanged.AddUniqueDynamic(this, &UWorldHub_HistorySubsystem::OnHubValueChanged);
	}
	return Resolved;
}

TScriptInterface<ISeam_SimClock> UWorldHub_HistorySubsystem::ResolveClock()
{
	if (UObject* Cached = CachedClockObject.Get())
	{
		TScriptInterface<ISeam_SimClock> Result;
		Result.SetObject(Cached);
		Result.SetInterface(Cast<ISeam_SimClock>(Cached));
		if (Result.GetInterface())
		{
			return Result;
		}
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		static const FGameplayTag ClockKey =
			FGameplayTag::RequestGameplayTag(FName("DP.Service.SimClock"), /*ErrorIfNotFound=*/false);
		if (ClockKey.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(ClockKey))
			{
				if (ISeam_SimClock* AsClock = Cast<ISeam_SimClock>(Provider))
				{
					CachedClockObject = Provider;
					Clock = AsClock;
					TScriptInterface<ISeam_SimClock> Result;
					Result.SetObject(Provider);
					Result.SetInterface(AsClock);
					return Result;
				}
			}
		}
	}
	return TScriptInterface<ISeam_SimClock>();
}

//~ Tick ---------------------------------------------------------------------------------------

bool UWorldHub_HistorySubsystem::IsTickable() const
{
	// Never tick on clients (capture is authority-only) or before a hub exists.
	return HasWorldAuthority() && Hub.IsValid();
}

TStatId UWorldHub_HistorySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldHub_HistorySubsystem, STATGROUP_Tickables);
}

void UWorldHub_HistorySubsystem::Tick(float DeltaTime)
{
	if (!HasWorldAuthority())
	{
		return;
	}

	// Deterministic, pause-aware advance: skip entirely while paused; scale by the sim time-scale.
	double TimeScale = 1.0;
	if (TScriptInterface<ISeam_SimClock> ClockIf = ResolveClock())
	{
		if (ISeam_SimClock::Execute_IsPaused(ClockIf.GetObject()))
		{
			return;
		}
		TimeScale = ISeam_SimClock::Execute_GetTimeScale(ClockIf.GetObject());
	}

	const double ScaledDelta = static_cast<double>(DeltaTime) * TimeScale;
	SimClockSeconds += ScaledDelta;

	// Change-driven capture takes priority and resets the cadence so we don't double-capture.
	if (bCaptureOnChange && bDirtySinceLastFrame)
	{
		CaptureFrame(FGameplayTag());
		CadenceAccumulator = 0.0f;
		return;
	}

	if (CaptureCadenceSeconds > 0.0f)
	{
		CadenceAccumulator += static_cast<float>(ScaledDelta);
		if (CadenceAccumulator >= CaptureCadenceSeconds)
		{
			CadenceAccumulator -= CaptureCadenceSeconds;
			CaptureFrame(FGameplayTag());
		}
	}
}

void UWorldHub_HistorySubsystem::OnHubValueChanged(FWorldHub_Scope /*Scope*/, FGameplayTag /*Key*/, FSeam_NetValue /*NewValue*/)
{
	bDirtySinceLastFrame = true;
}

//~ Capture / rewind ---------------------------------------------------------------------------

int32 UWorldHub_HistorySubsystem::CaptureFrame(FGameplayTag LabelTag)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return INDEX_NONE;
	}
	UWorldHub_StateHubSubsystem* H = ResolveHub();
	if (!H)
	{
		return INDEX_NONE;
	}

	FWorldHub_HistoryFrame Frame;
	Frame.FrameIndex = NextFrameIndex++;
	Frame.LabelTag = LabelTag;
	Frame.SimTimeSeconds = SimClockSeconds;

	if (TScriptInterface<ISeam_SimClock> ClockIf = ResolveClock())
	{
		Frame.DayNumber = ISeam_SimClock::Execute_GetDayNumber(ClockIf.GetObject());
	}

	// Reuse the hub's existing ISeam_Persistable capture path: it produces an FWorldHub_Snapshot.
	FInstancedStruct Captured;
	H->CaptureState_Implementation(Captured);
	if (Captured.IsValid() && Captured.GetScriptStruct() == FWorldHub_Snapshot::StaticStruct())
	{
		Frame.Snapshot = Captured.Get<FWorldHub_Snapshot>();
	}

	Frames.Add(MoveTemp(Frame));

	// Evict oldest beyond the cap.
	while (MaxFrames > 0 && Frames.Num() > MaxFrames)
	{
		Frames.RemoveAt(0, 1, /*bAllowShrinking=*/false);
	}

	bDirtySinceLastFrame = false;

	const int32 CapturedIndex = NextFrameIndex - 1;
	OnFrameCaptured.Broadcast(CapturedIndex, LabelTag);
	BroadcastHistoryBus(WorldHubNativeTags::Bus_WorldHub_FrameCaptured, CapturedIndex, LabelTag, SimClockSeconds);

	UE_LOG(LogDP, Verbose, TEXT("[WorldHub] Captured frame %d (label=%s, entries=%d)."),
		CapturedIndex, *LabelTag.ToString(), Frames.Last().Snapshot.Num());

	return CapturedIndex;
}

int32 UWorldHub_HistorySubsystem::FindFramePos(int32 FrameIndex) const
{
	for (int32 i = Frames.Num() - 1; i >= 0; --i)
	{
		if (Frames[i].FrameIndex == FrameIndex)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool UWorldHub_HistorySubsystem::RewindToFrame(int32 FrameIndex)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return false;
	}
	const int32 Pos = FindFramePos(FrameIndex);
	if (Pos == INDEX_NONE)
	{
		return false;
	}

	const FWorldHub_HistoryFrame& Frame = Frames[Pos];
	ApplySnapshotToHub(Frame.Snapshot);

	BroadcastHistoryBus(WorldHubNativeTags::Bus_WorldHub_Rewound, Frame.FrameIndex, Frame.LabelTag, Frame.SimTimeSeconds);
	UE_LOG(LogDP, Log, TEXT("[WorldHub] Rewound to frame %d (label=%s)."), Frame.FrameIndex, *Frame.LabelTag.ToString());
	return true;
}

bool UWorldHub_HistorySubsystem::RewindToLabel(FGameplayTag CheckpointLabel)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority() || !CheckpointLabel.IsValid())
	{
		return false;
	}
	// Most-recent frame carrying the label.
	for (int32 i = Frames.Num() - 1; i >= 0; --i)
	{
		if (Frames[i].LabelTag == CheckpointLabel)
		{
			return RewindToFrame(Frames[i].FrameIndex);
		}
	}
	return false;
}

bool UWorldHub_HistorySubsystem::RewindToTimestamp(double TargetSimTime)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return false;
	}
	// Latest frame at or before the target time (frames are appended in increasing time order).
	int32 BestPos = INDEX_NONE;
	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		if (Frames[i].SimTimeSeconds <= TargetSimTime)
		{
			BestPos = i;
		}
		else
		{
			break;
		}
	}
	if (BestPos == INDEX_NONE)
	{
		return false;
	}
	return RewindToFrame(Frames[BestPos].FrameIndex);
}

void UWorldHub_HistorySubsystem::ApplySnapshotToHub(const FWorldHub_Snapshot& Snapshot)
{
	UWorldHub_StateHubSubsystem* H = ResolveHub();
	if (!H)
	{
		return;
	}
	// Re-apply each captured slot through the hub's authoritative SetValue (so clients re-mirror it).
	for (const FWorldHub_SnapshotEntry& Entry : Snapshot.Entries)
	{
		H->SetValue(Entry.Key, Entry.Scope, Entry.Value);
	}
}

//~ Read / diff --------------------------------------------------------------------------------

bool UWorldHub_HistorySubsystem::GetFrame(int32 FrameIndex, FWorldHub_HistoryFrame& Out) const
{
	const int32 Pos = FindFramePos(FrameIndex);
	if (Pos == INDEX_NONE)
	{
		return false;
	}
	Out = Frames[Pos];
	return true;
}

void UWorldHub_HistorySubsystem::DiffFrames(int32 FromIndex, int32 ToIndex, TArray<FWorldHub_StateDelta>& Out) const
{
	Out.Reset();
	const int32 FromPos = FindFramePos(FromIndex);
	const int32 ToPos = FindFramePos(ToIndex);
	if (FromPos == INDEX_NONE || ToPos == INDEX_NONE)
	{
		return;
	}

	const TArray<FWorldHub_SnapshotEntry>& From = Frames[FromPos].Snapshot.Entries;
	const TArray<FWorldHub_SnapshotEntry>& To = Frames[ToPos].Snapshot.Entries;

	// Index the FROM frame by (Scope, Key) for O(N+M) comparison.
	TMap<FWorldHub_SlotAddress, const FWorldHub_SnapshotEntry*> FromIndexMap;
	FromIndexMap.Reserve(From.Num());
	for (const FWorldHub_SnapshotEntry& E : From)
	{
		FromIndexMap.Add(FWorldHub_SlotAddress(E.Scope, E.Key), &E);
	}

	TSet<FWorldHub_SlotAddress> ToSeen;
	ToSeen.Reserve(To.Num());

	for (const FWorldHub_SnapshotEntry& E : To)
	{
		const FWorldHub_SlotAddress Addr(E.Scope, E.Key);
		ToSeen.Add(Addr);

		const FWorldHub_SnapshotEntry* const* FoundPtr = FromIndexMap.Find(Addr);
		if (!FoundPtr)
		{
			FWorldHub_StateDelta& Delta = Out.AddDefaulted_GetRef();
			Delta.Scope = E.Scope;
			Delta.Key = E.Key;
			Delta.Kind = EWorldHub_DeltaKind::Added;
			Delta.NextValue = E.Value;
		}
		else
		{
			const FWorldHub_SnapshotEntry& Prev = **FoundPtr;
			if (!(Prev.Value.Value == E.Value.Value))
			{
				FWorldHub_StateDelta& Delta = Out.AddDefaulted_GetRef();
				Delta.Scope = E.Scope;
				Delta.Key = E.Key;
				Delta.Kind = EWorldHub_DeltaKind::Changed;
				Delta.PreviousValue = Prev.Value;
				Delta.NextValue = E.Value;
			}
		}
	}

	// Anything in FROM but not in TO was removed.
	for (const FWorldHub_SnapshotEntry& E : From)
	{
		const FWorldHub_SlotAddress Addr(E.Scope, E.Key);
		if (!ToSeen.Contains(Addr))
		{
			FWorldHub_StateDelta& Delta = Out.AddDefaulted_GetRef();
			Delta.Scope = E.Scope;
			Delta.Key = E.Key;
			Delta.Kind = EWorldHub_DeltaKind::Removed;
			Delta.PreviousValue = E.Value;
		}
	}
}

//~ ISeam_HubHistory ---------------------------------------------------------------------------

bool UWorldHub_HistorySubsystem::RewindToCheckpoint(FGameplayTag CheckpointLabel)
{
	// Authority-gated inside RewindToLabel.
	return RewindToLabel(CheckpointLabel);
}

int64 UWorldHub_HistorySubsystem::GetLatestEventSequence() const
{
	// The history subsystem treats each captured frame as an "event"; the latest sequence is the
	// last frame's monotonic index (+1 so an empty buffer reports 0, matching the seam contract).
	return Frames.Num() > 0 ? static_cast<int64>(Frames.Last().FrameIndex) + 1 : 0;
}

void UWorldHub_HistorySubsystem::GetEventsSince(int64 Sequence, TArray<FInstancedStruct>& OutFlattened) const
{
	OutFlattened.Reset();
	for (const FWorldHub_HistoryFrame& Frame : Frames)
	{
		if (static_cast<int64>(Frame.FrameIndex) + 1 > Sequence)
		{
			// Flatten a frame to a LOCAL FInstancedStruct (a compact bus payload form).
			FWorldHub_HistoryBusPayload Payload(Frame.FrameIndex, Frame.LabelTag, Frame.SimTimeSeconds);
			FInstancedStruct& Boxed = OutFlattened.AddDefaulted_GetRef();
			Boxed.InitializeAs<FWorldHub_HistoryBusPayload>(Payload);
		}
	}
}

//~ Bus / service ------------------------------------------------------------------------------

void UWorldHub_HistorySubsystem::BroadcastHistoryBus(const FGameplayTag& Channel, int32 FrameIndex, const FGameplayTag& Label, double SimTime) const
{
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FWorldHub_HistoryBusPayload Payload(FrameIndex, Label, SimTime);
		FInstancedStruct Wrapped;
		Wrapped.InitializeAs<FWorldHub_HistoryBusPayload>(Payload);
		Bus->BroadcastPayload(Channel, Wrapped, const_cast<UWorldHub_HistorySubsystem*>(this));
	}
}

void UWorldHub_HistorySubsystem::RegisterSelfAsService(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}
	if (bRegister)
	{
		// WeakObserved: GameInstance-scoped locator must not keep a dead world's subsystem alive.
		Locator->RegisterService(WorldHubNativeTags::Service_WorldHub_History, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else if (Locator->ResolveService(WorldHubNativeTags::Service_WorldHub_History) == this)
	{
		Locator->UnregisterService(WorldHubNativeTags::Service_WorldHub_History);
	}
}

//~ Debug --------------------------------------------------------------------------------------

FString UWorldHub_HistorySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("WorldHub History [%s] Frames=%d/%d NextIdx=%d Sim=%.1fs Dirty=%s"),
		HasWorldAuthority() ? TEXT("AUTH") : TEXT("client"),
		Frames.Num(), MaxFrames, NextFrameIndex, SimClockSeconds,
		bDirtySinceLastFrame ? TEXT("yes") : TEXT("no"));
}
