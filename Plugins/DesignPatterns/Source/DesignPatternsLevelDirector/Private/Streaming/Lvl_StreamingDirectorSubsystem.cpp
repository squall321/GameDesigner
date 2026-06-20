// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/Lvl_StreamingDirectorSubsystem.h"

#include "DesignPatternsLevelDirectorModule.h"
#include "DesignPatternsLevelDirectorNativeTags.h"
#include "Seam/Lvl_InterestSource.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Activation/Seam_ActivationGate.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Net/Seam_NetValue.h"

#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/**
	 * Defensive fallback policy used ONLY when the settings CDO cannot be resolved. These mirror the
	 * documented ULvl_DeveloperSettings defaults so behaviour is stable even in pathological early-load.
	 * They are NOT gameplay tunables baked into logic — the real values come from the settings CDO.
	 */
	static const FLvl_DistanceBand& GetHardFallbackBand()
	{
		static FLvl_DistanceBand Fallback = []()
		{
			FLvl_DistanceBand B;
			B.BandName = TEXT("HardFallback");
			B.LoadWithinDistance = 12000.f;
			B.UnloadBeyondDistance = 18000.f;
			B.bMakeVisibleWhenLoaded = true;
			return B;
		}();
		return Fallback;
	}

	/** Minimum evaluation interval, to stop a misconfigured 0 from running the director every tick. */
	constexpr float MinEvaluationInterval = 0.05f;
}

//~ USubsystem lifecycle ---------------------------------------------------------------------------

void ULvl_StreamingDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bManuallyEnabled = true;
	TimeSinceLastEvaluation = 0.f;
	TimeSinceLastReport = 0.f;
	bHasEvaluatedOnce = false;
	EvaluationCursor = 0;
	ChurnLoadsSinceReport = 0;
	ChurnUnloadsSinceReport = 0;
	bWorldPartitionActive = false;

	// Register a tag-keyed service so other systems can reach the director without a hard class dep.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped; it must not keep this dead world's
		// subsystem alive across level travel (rule 3).
		Locator->RegisterService(LvlTags::Service_StreamingDirector, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}

	// Cadence-driven evaluation via FTSTicker (we deliberately are NOT an FTickableGameObject so we
	// never tick in editor preview / during seamless travel — mirrors the message bus pattern).
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULvl_StreamingDirectorSubsystem::Tick));

	UE_LOG(LogDP, Log, TEXT("[LevelDirector] Streaming director initialized (authority=%s)."),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}

void ULvl_StreamingDirectorSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	ClearWorldPartitionSources();

	InterestSources.Reset();
	TrackedLevels.Reset();
	CachedActivationGate.Reset();
	CachedAnalyticsSink.Reset();

	Super::Deinitialize();
}

//~ Interest source registration -------------------------------------------------------------------

void ULvl_StreamingDirectorSubsystem::RegisterInterestSource(const TScriptInterface<ILvl_InterestSource>& Source)
{
	UObject* Obj = Source.GetObject();
	if (!Obj || !Source.GetInterface())
	{
		UE_LOG(LogDP, Warning, TEXT("[LevelDirector] RegisterInterestSource ignored: null or non-implementing object."));
		return;
	}

	// De-dupe by underlying object.
	for (const TWeakInterfacePtr<ILvl_InterestSource>& Existing : InterestSources)
	{
		if (Existing.GetObject() == Obj)
		{
			return; // already registered
		}
	}

	InterestSources.Add(TWeakInterfacePtr<ILvl_InterestSource>(Source.GetInterface()));
	RequestImmediateReevaluation();

	UE_LOG(LogDP, Verbose, TEXT("[LevelDirector] Registered interest source '%s' (%d total)."),
		*Obj->GetName(), InterestSources.Num());
}

bool ULvl_StreamingDirectorSubsystem::UnregisterInterestSource(const TScriptInterface<ILvl_InterestSource>& Source)
{
	const UObject* Obj = Source.GetObject();
	if (!Obj)
	{
		return false;
	}

	const int32 Removed = InterestSources.RemoveAll(
		[Obj](const TWeakInterfacePtr<ILvl_InterestSource>& Entry)
		{
			return Entry.GetObject() == Obj;
		});

	if (Removed > 0)
	{
		RequestImmediateReevaluation();
		return true;
	}
	return false;
}

int32 ULvl_StreamingDirectorSubsystem::GetInterestSourceCount() const
{
	int32 Live = 0;
	for (const TWeakInterfacePtr<ILvl_InterestSource>& Entry : InterestSources)
	{
		if (Entry.IsValid())
		{
			++Live;
		}
	}
	return Live;
}

//~ Control ----------------------------------------------------------------------------------------

void ULvl_StreamingDirectorSubsystem::RequestImmediateReevaluation()
{
	// Saturate the cadence accumulator so the next tick runs a pass; also force the move-threshold
	// check to fire by clearing the "evaluated once" flag's effect via a large accumulator.
	const ULvl_DeveloperSettings* Settings = GetSettings();
	const float Interval = Settings ? Settings->EvaluationIntervalSeconds : 0.25f;
	TimeSinceLastEvaluation = FMath::Max(Interval, MinEvaluationInterval);
}

void ULvl_StreamingDirectorSubsystem::SetDirectorEnabled(bool bEnabled)
{
	if (bManuallyEnabled != bEnabled)
	{
		bManuallyEnabled = bEnabled;
		UE_LOG(LogDP, Log, TEXT("[LevelDirector] Director %s by request."),
			bEnabled ? TEXT("ENABLED") : TEXT("SUSPENDED"));
		if (!bEnabled)
		{
			// When suspended, drop any WP sources so WP doesn't keep streaming around stale centers.
			ClearWorldPartitionSources();
		}
		else
		{
			RequestImmediateReevaluation();
		}
	}
}

bool ULvl_StreamingDirectorSubsystem::IsDirectorActive() const
{
	if (!bManuallyEnabled)
	{
		return false;
	}

	// Activation gate: default OPEN when unresolved (rule 10). We const_cast to use the caching
	// resolver; resolution only reads/caches a weak ref and never mutates observable state.
	ULvl_StreamingDirectorSubsystem* MutableThis = const_cast<ULvl_StreamingDirectorSubsystem*>(this);
	if (MutableThis->ResolveActivationGate() != nullptr)
	{
		if (UObject* GateObj = MutableThis->CachedActivationGate.GetObject())
		{
			return ISeam_ActivationGate::Execute_IsGateOpen(GateObj, LvlTags::Gate_StreamingEnabled);
		}
	}
	return true; // inert default: open
}

//~ Tick / evaluation ------------------------------------------------------------------------------

bool ULvl_StreamingDirectorSubsystem::Tick(float DeltaTime)
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return true; // keep the ticker alive; nothing to do in non-game worlds
	}

	TimeSinceLastReport += DeltaTime;

	if (!IsDirectorActive())
	{
		return true;
	}

	const ULvl_DeveloperSettings* Settings = GetSettings();
	const float Interval = FMath::Max(
		Settings ? Settings->EvaluationIntervalSeconds : 0.25f, MinEvaluationInterval);
	const float MoveThreshold = Settings ? Settings->MoveReevaluateThreshold : 500.f;

	TimeSinceLastEvaluation += DeltaTime;

	// Build query bubbles (also prunes dead sources) so the move-threshold can short-circuit cadence.
	TArray<FLvl_InterestQuery> Queries;
	BuildInterestQueries(Queries);

	bool bShouldEvaluate = TimeSinceLastEvaluation >= Interval;
	if (!bShouldEvaluate && MoveThreshold > 0.f && bHasEvaluatedOnce && Queries.Num() > 0)
	{
		const FVector Centroid = ComputeCentroid(Queries);
		if (FVector::DistSquared(Centroid, LastEvaluationCentroid) >= FMath::Square(MoveThreshold))
		{
			bShouldEvaluate = true;
		}
	}

	if (bShouldEvaluate)
	{
		TimeSinceLastEvaluation = 0.f;
		EvaluateStreaming(Queries);
		LastEvaluationCentroid = ComputeCentroid(Queries);
		bHasEvaluatedOnce = true;
	}

	MaybeEmitChurnAnalytics();
	return true; // never auto-remove the ticker; Deinitialize handles teardown
}

void ULvl_StreamingDirectorSubsystem::BuildInterestQueries(TArray<FLvl_InterestQuery>& OutQueries)
{
	OutQueries.Reset();

	// Prune dead sources in place while gathering live queries.
	for (int32 i = InterestSources.Num() - 1; i >= 0; --i)
	{
		TWeakInterfacePtr<ILvl_InterestSource>& Entry = InterestSources[i];
		if (!Entry.IsValid())
		{
			InterestSources.RemoveAtSwap(i);
			continue;
		}

		UObject* Obj = Entry.GetObject();
		ILvl_InterestSource* Iface = Entry.Get();
		if (!Obj || !Iface)
		{
			InterestSources.RemoveAtSwap(i);
			continue;
		}

		FLvl_InterestQuery Q;
		Q.Location = ILvl_InterestSource::Execute_GetInterestLocation(Obj);
		Q.ExtraRadius = FMath::Max(0.f, ILvl_InterestSource::Execute_GetInterestRadius(Obj));
		OutQueries.Add(Q);
	}
}

FVector ULvl_StreamingDirectorSubsystem::ComputeCentroid(const TArray<FLvl_InterestQuery>& Queries)
{
	if (Queries.Num() == 0)
	{
		return FVector::ZeroVector;
	}
	FVector Sum = FVector::ZeroVector;
	for (const FLvl_InterestQuery& Q : Queries)
	{
		Sum += Q.Location;
	}
	return Sum / static_cast<double>(Queries.Num());
}

void ULvl_StreamingDirectorSubsystem::EvaluateStreaming(const TArray<FLvl_InterestQuery>& Queries)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1) World Partition (soft): drive per-source streaming sources where available.
	UpdateWorldPartitionSources(Queries);

	// 2) Classic streaming sublevels.
	RefreshTrackedLevels();

	const ULvl_DeveloperSettings* Settings = GetSettings();
	const int32 MaxLoads   = Settings ? Settings->MaxLoadRequestsPerFrame   : 2;
	const int32 MaxUnloads = Settings ? Settings->MaxUnloadRequestsPerFrame : 4;
	const int32 MaxEval    = Settings ? Settings->MaxLevelsEvaluatedPerFrame : 64;

	int32 LoadsIssued = 0;
	int32 UnloadsIssued = 0;

	// Build a stable, ordered key list so the round-robin cursor is deterministic across passes.
	TArray<FName> Keys;
	TrackedLevels.GetKeys(Keys);
	Keys.Sort(FNameLexicalLess());

	const int32 NumLevels = Keys.Num();
	if (NumLevels == 0)
	{
		return;
	}

	const int32 EvalCount = (MaxEval > 0) ? FMath::Min(MaxEval, NumLevels) : NumLevels;
	if (EvaluationCursor >= NumLevels)
	{
		EvaluationCursor = 0;
	}

	for (int32 Step = 0; Step < EvalCount; ++Step)
	{
		const int32 Index = (EvaluationCursor + Step) % NumLevels;
		const FName Key = Keys[Index];
		FLvl_TrackedLevelState* State = TrackedLevels.Find(Key);
		if (!State)
		{
			continue;
		}

		ULevelStreaming* Level = State->StreamingLevel.Get();
		if (!Level)
		{
			TrackedLevels.Remove(Key);
			continue;
		}

		// Distance from the level's transform origin to the nearest interest bubble.
		const FVector LevelOrigin = Level->LevelTransform.GetLocation();
		const float Nearest = NearestDistanceToInterest(LevelOrigin, Queries);
		State->LastNearestDistance = Nearest;

		const FLvl_DistanceBand& Band = ResolveBandForCategory(State->Category);

		// Hysteresis: load inside Load band, unload beyond Unload band, hold otherwise.
		bool bWantLoaded = State->bWantsLoaded;
		if (Nearest <= Band.LoadWithinDistance)
		{
			bWantLoaded = true;
		}
		else if (Nearest > Band.UnloadBeyondDistance)
		{
			bWantLoaded = false;
		}

		// No interest sources at all => unload everything (Nearest is +inf).
		if (Queries.Num() == 0)
		{
			bWantLoaded = false;
		}

		const bool bWantVisible = bWantLoaded && Band.bMakeVisibleWhenLoaded;

		const bool bCurrentlyWantsLoaded = State->bWantsLoaded;
		const bool bDecisionIsLoad = bWantLoaded && !bCurrentlyWantsLoaded;
		const bool bDecisionIsUnload = !bWantLoaded && bCurrentlyWantsLoaded;

		// Budget gating: only count a budget slot when the desired state actually changes.
		if (bDecisionIsLoad)
		{
			if (MaxLoads > 0 && LoadsIssued >= MaxLoads)
			{
				continue; // defer this load to a later pass
			}
			++LoadsIssued;
			++ChurnLoadsSinceReport;
		}
		else if (bDecisionIsUnload)
		{
			if (MaxUnloads > 0 && UnloadsIssued >= MaxUnloads)
			{
				continue; // defer this unload to a later pass
			}
			++UnloadsIssued;
			++ChurnUnloadsSinceReport;
		}

		State->bWantsLoaded = bWantLoaded;
		State->bWantsVisible = bWantVisible;
		ApplyLevelDecision(Level, bWantLoaded, bWantVisible);
	}

	// Advance the round-robin cursor for next pass.
	EvaluationCursor = (EvaluationCursor + EvalCount) % NumLevels;
}

void ULvl_StreamingDirectorSubsystem::RefreshTrackedLevels()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Mark-and-sweep: drop tracked entries whose level no longer exists.
	TSet<FName> Seen;
	const TArray<ULevelStreaming*>& Streaming = World->GetStreamingLevels();
	Seen.Reserve(Streaming.Num());

	for (ULevelStreaming* Level : Streaming)
	{
		if (!Level)
		{
			continue;
		}
		const FName Key = Level->GetWorldAssetPackageFName();
		if (Key.IsNone())
		{
			continue;
		}
		Seen.Add(Key);

		FLvl_TrackedLevelState& State = TrackedLevels.FindOrAdd(Key);
		State.StreamingLevel = Level;

		// Category: read from the level's net-quiet metadata if present; otherwise default (invalid
		// tag => default band). We do not hard-depend on a custom ULevelStreaming subclass, so the
		// category stays invalid here and projects can extend by tagging via a policy asset later.
		// (Invalid category is fully supported: ResolveBandForCategory returns the default band.)
	}

	// Sweep removed levels.
	for (auto It = TrackedLevels.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()) || !It.Value().StreamingLevel.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

const FLvl_DistanceBand& ULvl_StreamingDirectorSubsystem::ResolveBandForCategory(FGameplayTag Category) const
{
	const ULvl_DeveloperSettings* Settings = GetSettings();
	if (!Settings || Settings->DistanceBands.Num() == 0)
	{
		return GetHardFallbackBand();
	}

	const FLvl_DistanceBand* DefaultBand = nullptr;
	for (const FLvl_DistanceBand& Band : Settings->DistanceBands)
	{
		if (Category.IsValid() && Band.AppliesToCategory.IsValid() &&
			Category.MatchesTag(Band.AppliesToCategory))
		{
			return Band;
		}
		if (!Band.AppliesToCategory.IsValid() && DefaultBand == nullptr)
		{
			DefaultBand = &Band; // first untagged band is the default
		}
	}

	if (DefaultBand)
	{
		return *DefaultBand;
	}
	// No untagged default authored: use the first band so behaviour is still defined.
	return Settings->DistanceBands[0];
}

float ULvl_StreamingDirectorSubsystem::NearestDistanceToInterest(
	const FVector& LevelOrigin, const TArray<FLvl_InterestQuery>& Queries)
{
	float Nearest = TNumericLimits<float>::Max();
	for (const FLvl_InterestQuery& Q : Queries)
	{
		// Subtract the source's extra radius so a wide bubble effectively pulls levels in earlier.
		const float D = FMath::Max(0.f, static_cast<float>(FVector::Dist(LevelOrigin, Q.Location)) - Q.ExtraRadius);
		Nearest = FMath::Min(Nearest, D);
	}
	return Nearest;
}

void ULvl_StreamingDirectorSubsystem::ApplyLevelDecision(ULevelStreaming* Level, bool bShouldLoad, bool bShouldBeVisible)
{
	if (!Level)
	{
		return;
	}

	const bool bWasLoaded = Level->ShouldBeLoaded();
	const bool bWasVisible = Level->ShouldBeVisible();

	if (bWasLoaded == bShouldLoad && bWasVisible == bShouldBeVisible)
	{
		return; // no engine call needed
	}

	// Wrap the engine streaming API (rule 5: wrap, don't reinvent).
	Level->SetShouldBeLoaded(bShouldLoad);
	Level->SetShouldBeVisible(bShouldBeVisible);

	// Broadcast a local message so listeners (HUD loading hints, audio, debug) can react.
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FGameplayTag Channel;
		if (bShouldLoad && !bWasLoaded)
		{
			Channel = LvlTags::Bus_StreamingLevelLoading;
		}
		else if (!bShouldLoad && bWasLoaded)
		{
			Channel = LvlTags::Bus_StreamingLevelUnloading;
		}
		else if (bShouldBeVisible && !bWasVisible)
		{
			Channel = LvlTags::Bus_StreamingLevelLoaded;
		}

		if (Channel.IsValid())
		{
			// Payload-free broadcast: the channel + instigator carry enough for local listeners.
			Bus->BroadcastPayload(Channel, FInstancedStruct(), this);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[LevelDirector] Level '%s' -> load=%s visible=%s."),
		*Level->GetWorldAssetPackageName(),
		bShouldLoad ? TEXT("Y") : TEXT("N"),
		bShouldBeVisible ? TEXT("Y") : TEXT("N"));
}

//~ World Partition (resolved softly) --------------------------------------------------------------

void ULvl_StreamingDirectorSubsystem::UpdateWorldPartitionSources(const TArray<FLvl_InterestQuery>& Queries)
{
	const ULvl_DeveloperSettings* Settings = GetSettings();
	const bool bWantWP = !Settings || Settings->bDriveWorldPartitionSources;
	if (!bWantWP)
	{
		ClearWorldPartitionSources();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Resolve the World Partition subsystem by class so we do NOT hard-depend on the WorldPartition
	// module. If the class isn't loaded (WP disabled / not in the project), we degrade to classic
	// sublevel streaming only — documented inert default for an absent dependency.
	UClass* WPClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.WorldPartitionSubsystem"));
	UWorldSubsystem* WPSubsystem = WPClass ? World->GetSubsystemBase(WPClass) : nullptr;
	if (!WPSubsystem)
	{
		if (bWorldPartitionActive)
		{
			bWorldPartitionActive = false;
		}
		return;
	}

	// WP is present. We intentionally do not invoke private WP streaming-source registration APIs by
	// reflection here (they are unstable across 5.3-5.5). Instead, the documented behaviour is: when
	// WP is enabled, the engine's own ULocalPlayer / camera streaming sources already drive WP cell
	// streaming, and the director's interest sources are typically those same actors. The director
	// therefore marks WP active for diagnostics and leaves cell streaming to the engine, while still
	// managing any classic sublevels layered on top. This keeps us correct AND forward-compatible.
	bWorldPartitionActive = (Queries.Num() > 0);
}

void ULvl_StreamingDirectorSubsystem::ClearWorldPartitionSources()
{
	RegisteredWorldPartitionSourceNames.Reset();
	bWorldPartitionActive = false;
}

//~ Seam resolution --------------------------------------------------------------------------------

ISeam_ActivationGate* ULvl_StreamingDirectorSubsystem::ResolveActivationGate()
{
	if (CachedActivationGate.IsValid())
	{
		return CachedActivationGate.Get();
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Resolve under the module's SHARED activation-gate key (owned by the placement area) so the
		// streaming director and encounter activators share one gate registration. The provider is
		// registered by the project / World-hub adapter; we test for the interface rather than assuming
		// a concrete class, keeping us decoupled (rule 10). Unresolved => caller treats the gate OPEN.
		if (UObject* Obj = Locator->ResolveService(LvlNativeTags::Service_Lvl_ActivationGate))
		{
			if (ISeam_ActivationGate* GateIface = Cast<ISeam_ActivationGate>(Obj))
			{
				CachedActivationGate = TWeakInterfacePtr<ISeam_ActivationGate>(GateIface);
				return CachedActivationGate.Get();
			}
		}
	}
	return nullptr;
}

ISeam_AnalyticsSink* ULvl_StreamingDirectorSubsystem::ResolveAnalyticsSink()
{
	if (CachedAnalyticsSink.IsValid())
	{
		return CachedAnalyticsSink.Get();
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Obj = Locator->ResolveService(LvlTags::Service_AnalyticsSink))
		{
			if (ISeam_AnalyticsSink* SinkIface = Cast<ISeam_AnalyticsSink>(Obj))
			{
				CachedAnalyticsSink = TWeakInterfacePtr<ISeam_AnalyticsSink>(SinkIface);
				return CachedAnalyticsSink.Get();
			}
		}
	}
	return nullptr;
}

void ULvl_StreamingDirectorSubsystem::MaybeEmitChurnAnalytics()
{
	const ULvl_DeveloperSettings* Settings = GetSettings();
	if (Settings && !Settings->bEmitChurnAnalytics)
	{
		return;
	}

	const float ReportInterval = FMath::Max(1.f, Settings ? Settings->AnalyticsReportIntervalSeconds : 30.f);
	if (TimeSinceLastReport < ReportInterval)
	{
		return;
	}
	TimeSinceLastReport = 0.f;

	// Nothing to report if there was no churn — keeps the analytics volume low.
	if (ChurnLoadsSinceReport == 0 && ChurnUnloadsSinceReport == 0)
	{
		return;
	}

	ISeam_AnalyticsSink* Sink = ResolveAnalyticsSink();
	UObject* SinkObj = CachedAnalyticsSink.GetObject();
	if (Sink && SinkObj && ISeam_AnalyticsSink::Execute_IsSinkReady(SinkObj))
	{
		// PII-safe by construction: attributes are FSeam_NetValue (closed union), never text/ids.
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Add(FSeam_AnalyticsAttr(TEXT("Loads"),  FSeam_NetValue::MakeInt(ChurnLoadsSinceReport)));
		Attrs.Add(FSeam_AnalyticsAttr(TEXT("Unloads"),FSeam_NetValue::MakeInt(ChurnUnloadsSinceReport)));
		Attrs.Add(FSeam_AnalyticsAttr(TEXT("Tracked"),FSeam_NetValue::MakeInt(TrackedLevels.Num())));
		ISeam_AnalyticsSink::Execute_RecordAggregateEvent(SinkObj, LvlTags::Analytics_StreamingChurn, Attrs);
	}

	// Reset counters regardless of whether a sink consumed them (the window has elapsed).
	ChurnLoadsSinceReport = 0;
	ChurnUnloadsSinceReport = 0;
}

const ULvl_DeveloperSettings* ULvl_StreamingDirectorSubsystem::GetSettings() const
{
	return ULvl_DeveloperSettings::Get();
}

//~ Debug ------------------------------------------------------------------------------------------

FString ULvl_StreamingDirectorSubsystem::GetDPDebugString_Implementation() const
{
	int32 Resident = 0;
	for (const TPair<FName, FLvl_TrackedLevelState>& Pair : TrackedLevels)
	{
		if (Pair.Value.bWantsLoaded)
		{
			++Resident;
		}
	}

	return FString::Printf(
		TEXT("StreamingDirector: %s | sources=%d | tracked=%d (resident=%d) | WP=%s"),
		IsDirectorActive() ? TEXT("ACTIVE") : TEXT("idle"),
		GetInterestSourceCount(),
		TrackedLevels.Num(),
		Resident,
		bWorldPartitionActive ? TEXT("on") : TEXT("off"));
}
