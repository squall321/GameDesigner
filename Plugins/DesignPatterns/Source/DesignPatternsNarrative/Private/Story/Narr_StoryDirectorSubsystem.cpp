// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Story/Narr_StoryDirectorSubsystem.h"
#include "Story/Narr_StoryBeatDataAsset.h"
#include "Story/Narr_StoryNativeTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"
#include "Data/DPDataRegistrySubsystem.h"

// World hub (PRIVATE dependency — never re-exported through our headers).
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"
#include "Query/WorldHub_Queryable.h"
#include "WorldHub_NativeTags.h"

// Clock seam availability probe.
#include "Clock/Seam_SimClock.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"

//~ USubsystem lifecycle -----------------------------------------------------------------------

void UNarr_StoryDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterServices();
	UE_LOG(LogDP, Log, TEXT("[Narr] Story director initialized."));
}

void UNarr_StoryDirectorSubsystem::Deinitialize()
{
	CachedHubQuery.Reset();
	ActiveBeats.Reset();
	CompletedBeats.Reset();
	StartedArcs.Reset();
	CompletedArcs.Reset();
	Super::Deinitialize();
}

bool UNarr_StoryDirectorSubsystem::HasWorldAuthority() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			return World->GetNetMode() != NM_Client;
		}
	}
	// No world yet (very early init): treat as authority so standalone setup is not blocked.
	return true;
}

void UNarr_StoryDirectorSubsystem::RegisterServices()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: we are a subsystem owned by the engine; the locator must not extend our life.
		Locator->RegisterService(NarrativeStoryNativeTags::Service_Narrative_ConditionSource, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
		Locator->RegisterService(NarrativeStoryNativeTags::Service_Narrative_StoryDirector, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ Hub resolution -----------------------------------------------------------------------------

IWorldHub_Queryable* UNarr_StoryDirectorSubsystem::GetHubQuery() const
{
	if (IWorldHub_Queryable* Cached = CachedHubQuery.Get())
	{
		return Cached;
	}

	// Prefer the locator (the hub self-registers under DP.Service.WorldHub). Fall back to the world
	// subsystem directly so the director works even before the hub has registered.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(WorldHubNativeTags::Service_WorldHub))
		{
			if (IWorldHub_Queryable* AsQuery = Cast<IWorldHub_Queryable>(Provider))
			{
				CachedHubQuery = TWeakInterfacePtr<IWorldHub_Queryable>(AsQuery);
				return AsQuery;
			}
		}
	}

	if (UWorldHub_StateHubSubsystem* Hub =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		IWorldHub_Queryable* AsQuery = Cast<IWorldHub_Queryable>(Hub);
		CachedHubQuery = TWeakInterfacePtr<IWorldHub_Queryable>(AsQuery);
		return AsQuery;
	}

	return nullptr;
}

UWorldHub_StateHubSubsystem* UNarr_StoryDirectorSubsystem::GetHubAuthority() const
{
	// Authoritative writes go through the concrete world subsystem (its mutators guard authority).
	return FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
}

UNarr_StoryBeatDataAsset* UNarr_StoryDirectorSubsystem::ResolveBeat(const FGameplayTag& BeatTag) const
{
	if (!BeatTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->FindByTag<UNarr_StoryBeatDataAsset>(BeatTag);
	}
	return nullptr;
}

//~ Progression API ----------------------------------------------------------------------------

bool UNarr_StoryDirectorSubsystem::RequestBeat(FGameplayTag BeatTag)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!BeatTag.IsValid())
	{
		return false;
	}
	if (ActiveBeats.Contains(BeatTag))
	{
		return true; // Idempotent.
	}

	UNarr_StoryBeatDataAsset* Beat = ResolveBeat(BeatTag);
	if (!Beat)
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] RequestBeat: unknown beat '%s'."), *BeatTag.ToString());
		return false;
	}

	if (!Beat->ArePrerequisitesMet(AsConditionSource()))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Narr] Beat '%s' blocked by prerequisites."), *BeatTag.ToString());
		BroadcastStoryEvent(NarrativeStoryNativeTags::Bus_Narrative_Story_BeatBlocked, BeatTag, Beat->ArcTag);
		return false;
	}

	// Activate.
	ActiveBeats.Add(BeatTag);

	const bool bArcWasStarted = Beat->ArcTag.IsValid() && StartedArcs.Contains(Beat->ArcTag);
	if (Beat->ArcTag.IsValid() && !bArcWasStarted)
	{
		StartedArcs.Add(Beat->ArcTag);
		BroadcastStoryEvent(NarrativeStoryNativeTags::Bus_Narrative_Story_ArcStarted, Beat->ArcTag, Beat->ArcTag);
	}

	UE_LOG(LogDP, Log, TEXT("[Narr] Beat '%s' started (arc '%s')."), *BeatTag.ToString(), *Beat->ArcTag.ToString());
	OnBeatStarted.Broadcast(BeatTag, Beat->ArcTag);
	BroadcastStoryEvent(NarrativeStoryNativeTags::Bus_Narrative_Story_BeatStarted, BeatTag, Beat->ArcTag);

	if (Beat->bAutoCompleteOnActivate)
	{
		CompleteBeat(BeatTag);
	}
	return true;
}

bool UNarr_StoryDirectorSubsystem::CompleteBeat(FGameplayTag BeatTag)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!ActiveBeats.Contains(BeatTag))
	{
		return false;
	}

	UNarr_StoryBeatDataAsset* Beat = ResolveBeat(BeatTag);

	// Apply unlock effects through ourselves (authority-guarded writes).
	if (Beat)
	{
		Beat->ApplyCompletionEffects(AsConditionSource());
	}

	// Mark the canonical completion flag in the world hub so other systems / dialogue can read it.
	const FGameplayTag CompletionFlag = GetBeatCompletionFlagKey(Beat, BeatTag);
	if (CompletionFlag.IsValid())
	{
		ApplyFlag(CompletionFlag, true);
	}

	ActiveBeats.Remove(BeatTag);
	CompletedBeats.Add(BeatTag);

	const FGameplayTag ArcTag = Beat ? Beat->ArcTag : FGameplayTag();
	UE_LOG(LogDP, Log, TEXT("[Narr] Beat '%s' completed."), *BeatTag.ToString());
	OnBeatCompleted.Broadcast(BeatTag, ArcTag);
	BroadcastStoryEvent(NarrativeStoryNativeTags::Bus_Narrative_Story_BeatCompleted, BeatTag, ArcTag);

	// Advance the branch graph, then see whether the arc is now finished.
	if (Beat)
	{
		AdvanceFromBeat(Beat);
		if (Beat->ArcTag.IsValid())
		{
			TryCompleteArc(Beat->ArcTag);
		}
	}
	return true;
}

void UNarr_StoryDirectorSubsystem::AdvanceFromBeat(const UNarr_StoryBeatDataAsset* CompletedBeatAsset)
{
	if (!CompletedBeatAsset)
	{
		return;
	}

	for (const FGameplayTag& NextTag : CompletedBeatAsset->NextBeats)
	{
		if (!NextTag.IsValid() || ActiveBeats.Contains(NextTag))
		{
			continue;
		}

		UNarr_StoryBeatDataAsset* NextBeat = ResolveBeat(NextTag);
		if (!NextBeat)
		{
			continue;
		}

		// Only consider candidates whose own prerequisites pass.
		if (!NextBeat->ArePrerequisitesMet(AsConditionSource()))
		{
			continue;
		}

		const bool bActivated = RequestBeat(NextTag);
		// Exclusive branch: stop after the first successful activation.
		if (bActivated && !CompletedBeatAsset->bAdvanceAllEligibleNext)
		{
			break;
		}
	}
}

void UNarr_StoryDirectorSubsystem::TryCompleteArc(const FGameplayTag& ArcTag)
{
	if (!ArcTag.IsValid() || CompletedArcs.Contains(ArcTag) || !StartedArcs.Contains(ArcTag))
	{
		return;
	}

	// The arc is complete when none of its beats are still active. We do not require knowing the full
	// beat set up front: an arc with no remaining active beats and no pending fan-out is "done". This
	// is a conservative emergent definition that works for linear and branching arcs alike.
	for (const FGameplayTag& Active : ActiveBeats)
	{
		if (UNarr_StoryBeatDataAsset* Beat = ResolveBeat(Active))
		{
			if (Beat->ArcTag == ArcTag)
			{
				return; // Still has an active beat in this arc.
			}
		}
	}

	CompletedArcs.Add(ArcTag);
	UE_LOG(LogDP, Log, TEXT("[Narr] Arc '%s' completed."), *ArcTag.ToString());
	BroadcastStoryEvent(NarrativeStoryNativeTags::Bus_Narrative_Story_ArcCompleted, ArcTag, ArcTag);
}

FGameplayTag UNarr_StoryDirectorSubsystem::GetBeatCompletionFlagKey(const UNarr_StoryBeatDataAsset* Beat, const FGameplayTag& BeatTag) const
{
	if (Beat && Beat->CompletionFlagOverride.IsValid())
	{
		return Beat->CompletionFlagOverride;
	}
	// Default: reuse the beat tag itself as the hub flag key. Projects that want a separate namespace
	// can set CompletionFlagOverride. The beat tag is a stable, unique identity so it is a valid key.
	return BeatTag;
}

void UNarr_StoryDirectorSubsystem::BroadcastStoryEvent(const FGameplayTag& Channel, const FGameplayTag& BeatTag, const FGameplayTag& ArcTag) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus || !Channel.IsValid())
	{
		return;
	}
	const FNarr_StoryEventPayload Payload(BeatTag, ArcTag);
	FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(Channel, Wrapped, const_cast<UNarr_StoryDirectorSubsystem*>(this));
}

bool UNarr_StoryDirectorSubsystem::HasClock() const
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// The clock owner registers under DP.Service.Clock by convention; we only probe availability.
		if (UObject* Provider = Locator->ResolveService(
			FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Clock"), /*ErrorIfNotFound=*/false)))
		{
			return Provider->Implements<USeam_SimClock>();
		}
	}
	return false;
}

//~ Blueprint-pure tracking queries ------------------------------------------------------------

bool UNarr_StoryDirectorSubsystem::IsArcStarted(FGameplayTag ArcTag) const
{
	return StartedArcs.Contains(ArcTag);
}

bool UNarr_StoryDirectorSubsystem::IsArcCompleted(FGameplayTag ArcTag) const
{
	return CompletedArcs.Contains(ArcTag);
}

//~ INarr_StoryConditionSource -----------------------------------------------------------------

TScriptInterface<INarr_StoryConditionSource> UNarr_StoryDirectorSubsystem::AsConditionSource() const
{
	TScriptInterface<INarr_StoryConditionSource> Iface;
	Iface.SetObject(const_cast<UNarr_StoryDirectorSubsystem*>(this));
	Iface.SetInterface(const_cast<UNarr_StoryDirectorSubsystem*>(this));
	return Iface;
}

bool UNarr_StoryDirectorSubsystem::QueryFlag(const FGameplayTag& Key, bool bDefault) const
{
	if (!Key.IsValid())
	{
		return bDefault;
	}
	// Prefer the concrete hub's typed reader (handles scope fallback + value-type decoding). The
	// IWorldHub_Queryable seam only yields a raw FInstancedStruct whose primitive payload we would have
	// to decode ourselves; the concrete read API is the supported path for typed flags/counters.
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		return Hub->QueryFlag(Key, FWorldHub_Scope::Global(), bDefault);
	}
	// Fallback: presence-only check via the read seam (a stored value of any type implies the flag).
	if (IWorldHub_Queryable* Query = GetHubQuery())
	{
		return Query->HasValue(Key, FWorldHub_Scope::Global()) ? true : bDefault;
	}
	return bDefault;
}

int64 UNarr_StoryDirectorSubsystem::QueryCounter(const FGameplayTag& Key, int64 Default) const
{
	if (!Key.IsValid())
	{
		return Default;
	}
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		return Hub->QueryCounter(Key, FWorldHub_Scope::Global(), Default);
	}
	return Default;
}

bool UNarr_StoryDirectorSubsystem::IsBeatActive(const FGameplayTag& BeatOrArcTag) const
{
	// Treat the interface query uniformly across beats and arcs.
	return ActiveBeats.Contains(BeatOrArcTag) || StartedArcs.Contains(BeatOrArcTag);
}

bool UNarr_StoryDirectorSubsystem::IsBeatCompleted(const FGameplayTag& BeatOrArcTag) const
{
	return CompletedBeats.Contains(BeatOrArcTag) || CompletedArcs.Contains(BeatOrArcTag);
}

void UNarr_StoryDirectorSubsystem::ApplyFlag(const FGameplayTag& Key, bool bValue)
{
	if (!HasWorldAuthority() || !Key.IsValid())
	{
		return;
	}
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		Hub->SetFlag(Key, bValue, FWorldHub_Scope::Global());
	}
}

int64 UNarr_StoryDirectorSubsystem::ApplyCounter(const FGameplayTag& Key, int64 Delta)
{
	if (!HasWorldAuthority() || !Key.IsValid())
	{
		return QueryCounter(Key, 0);
	}
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		return Hub->IncrementCounter(Key, Delta, FWorldHub_Scope::Global());
	}
	return QueryCounter(Key, 0);
}

//~ ISeam_Persistable --------------------------------------------------------------------------

void UNarr_StoryDirectorSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	// Capture is read-only; safe on any machine, but the director is GameInstance-scoped so the host's
	// record is the authoritative one. (Clients save nothing meaningful here.)
	FNarr_StorySaveRecord Record;
	Record.ActiveBeats = ActiveBeats.Array();
	Record.CompletedBeats = CompletedBeats.Array();
	Record.StartedArcs = StartedArcs.Array();
	Record.CompletedArcs = CompletedArcs.Array();
	Out = FInstancedStruct::Make(Record);
}

void UNarr_StoryDirectorSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY guard: a client-side load is a no-op (clients receive story flags via hub replication).
	if (!HasWorldAuthority())
	{
		return;
	}
	if (!In.IsValid() || In.GetScriptStruct() != FNarr_StorySaveRecord::StaticStruct())
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] RestoreState: record missing or wrong type; skipping."));
		return;
	}

	const FNarr_StorySaveRecord& Record = In.Get<FNarr_StorySaveRecord>();
	ActiveBeats.Reset();
	CompletedBeats.Reset();
	StartedArcs.Reset();
	CompletedArcs.Reset();
	ActiveBeats.Append(Record.ActiveBeats);
	CompletedBeats.Append(Record.CompletedBeats);
	StartedArcs.Append(Record.StartedArcs);
	CompletedArcs.Append(Record.CompletedArcs);

	UE_LOG(LogDP, Log, TEXT("[Narr] Story state restored: %d active, %d completed beats."),
		ActiveBeats.Num(), CompletedBeats.Num());
}

FGameplayTag UNarr_StoryDirectorSubsystem::GetPersistenceKind_Implementation() const
{
	return NarrativeStoryNativeTags::Persist_Narrative_Story;
}

//~ Debug --------------------------------------------------------------------------------------

FString UNarr_StoryDirectorSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("StoryDirector: active=%d completed=%d arcs(started=%d done=%d) authority=%s clock=%s"),
		ActiveBeats.Num(), CompletedBeats.Num(), StartedArcs.Num(), CompletedArcs.Num(),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"),
		HasClock() ? TEXT("yes") : TEXT("no"));
}
