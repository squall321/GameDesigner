// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reputation/Narr_ReputationSubsystem.h"
#include "Story/Narr_StoryNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

// World hub (PRIVATE dependency — resolved only here in .cpp).
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"
#include "Query/WorldHub_Queryable.h"
#include "Identity/Seam_EntityId.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"

//~ Lifecycle ----------------------------------------------------------------------------------

void UNarr_ReputationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterServices();
	UE_LOG(LogDP, Log, TEXT("[Narr] Reputation subsystem initialized."));
}

void UNarr_ReputationSubsystem::Deinitialize()
{
	CachedHubQuery.Reset();
	FactionStandings.Reset();
	NpcAffinities.Reset();
	Super::Deinitialize();
}

bool UNarr_ReputationSubsystem::HasWorldAuthority() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			return World->GetNetMode() != NM_Client;
		}
	}
	return true; // no world yet: treat as authority so standalone setup is not blocked
}

void UNarr_ReputationSubsystem::RegisterServices()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(NarrativeStoryNativeTags::Service_Narrative_Reputation, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

UWorldHub_StateHubSubsystem* UNarr_ReputationSubsystem::GetHubAuthority() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
}

FWorldHub_Scope UNarr_ReputationSubsystem::MakeNpcScope(const FGameplayTag& NpcTag) const
{
	// Derive a stable, deterministic guid from the NPC tag name so per-NPC affinity has an entity scope
	// without needing a live actor. Same tag -> same guid across runs/machines.
	const FString TagString = NpcTag.ToString();
	const uint32 Hash = GetTypeHash(TagString);
	// Build a deterministic guid from the hash (repeated into all four words for stability).
	const FGuid Deterministic(Hash, Hash ^ 0x9E3779B9u, Hash ^ 0x85EBCA6Bu, Hash ^ 0xC2B2AE35u);
	return FWorldHub_Scope::Entity(FSeam_EntityId(Deterministic));
}

//~ Mutators -----------------------------------------------------------------------------------

float UNarr_ReputationSubsystem::ApplyStanding(bool bFaction, const FGameplayTag& Tag, float NewValue)
{
	// In-memory mirror is the host's authoritative source for reads; the hub counter carries it on the wire.
	if (bFaction)
	{
		FactionStandings.Add(Tag, NewValue);
	}
	else
	{
		NpcAffinities.Add(Tag, NewValue);
	}

	// Mirror to the hub (rounded to int64 counter) so it replicates + saves through the hub's single path.
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		const FWorldHub_Scope Scope = bFaction
			? FWorldHub_Scope::Faction(Tag)
			: MakeNpcScope(Tag);
		// Use a fixed key under each scope (the scope already disambiguates faction/NPC identity); we key on
		// the tag itself so a single scope can hold its own standing entry.
		const int64 Rounded = static_cast<int64>(FMath::RoundToDouble(NewValue));
		// SetFlag/IncrementCounter both no-op on clients; we are authority-guarded by the caller. We want an
		// absolute set, so read-modify via increment against current.
		const int64 Current = Hub->QueryCounter(Tag, Scope, 0);
		Hub->IncrementCounter(Tag, Rounded - Current, Scope);
	}

	OnReputationChanged.Broadcast(Tag, NewValue);
	return NewValue;
}

float UNarr_ReputationSubsystem::AddFactionReputation(FGameplayTag FactionTag, float Delta)
{
	if (!HasWorldAuthority() || !FactionTag.IsValid())
	{
		return GetFactionReputation(FactionTag);
	}
	const float NewValue = GetFactionReputation(FactionTag) + Delta;
	return ApplyStanding(/*bFaction=*/true, FactionTag, NewValue);
}

float UNarr_ReputationSubsystem::AddNpcAffinity(FGameplayTag NpcTag, float Delta)
{
	if (!HasWorldAuthority() || !NpcTag.IsValid())
	{
		return GetNpcAffinity(NpcTag);
	}
	const float NewValue = GetNpcAffinity(NpcTag) + Delta;
	return ApplyStanding(/*bFaction=*/false, NpcTag, NewValue);
}

void UNarr_ReputationSubsystem::SetFactionReputation(FGameplayTag FactionTag, float Value)
{
	if (!HasWorldAuthority() || !FactionTag.IsValid())
	{
		return;
	}
	ApplyStanding(/*bFaction=*/true, FactionTag, Value);
}

//~ Reads --------------------------------------------------------------------------------------

float UNarr_ReputationSubsystem::GetFactionReputation(FGameplayTag FactionTag) const
{
	if (const float* Found = FactionStandings.Find(FactionTag))
	{
		return *Found;
	}
	// Client / post-travel: read the replicated hub counter.
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		return static_cast<float>(Hub->QueryCounter(FactionTag, FWorldHub_Scope::Faction(FactionTag), 0));
	}
	return 0.f;
}

float UNarr_ReputationSubsystem::GetNpcAffinity(FGameplayTag NpcTag) const
{
	if (const float* Found = NpcAffinities.Find(NpcTag))
	{
		return *Found;
	}
	if (UWorldHub_StateHubSubsystem* Hub = GetHubAuthority())
	{
		return static_cast<float>(Hub->QueryCounter(NpcTag, MakeNpcScope(NpcTag), 0));
	}
	return 0.f;
}

FGameplayTag UNarr_ReputationSubsystem::ResolveTier(float Value) const
{
	FGameplayTag Best;
	float BestMin = -FLT_MAX;
	for (const FNarr_ReputationTier& Tier : Tiers)
	{
		if (Value >= Tier.MinReputation && Tier.MinReputation >= BestMin)
		{
			BestMin = Tier.MinReputation;
			Best = Tier.TierTag;
		}
	}
	return Best;
}

//~ ISeam_Reputation ---------------------------------------------------------------------------

bool UNarr_ReputationSubsystem::HasReputation(const AActor* /*Subject*/) const
{
	// This subsystem is a real provider: reputation is always "tracked" once the subsystem exists. An empty
	// store still answers neutral, but HasReputation returns true so consumers know a provider is present.
	return true;
}

float UNarr_ReputationSubsystem::GetReputation(const AActor* /*Subject*/, FGameplayTag FactionTag) const
{
	// Subject-agnostic for the shipped owner: standing is global per faction (a single-player / shared-world
	// model). A project wanting per-subject reputation can subclass and key on the subject's entity id.
	return GetFactionReputation(FactionTag);
}

FGameplayTag UNarr_ReputationSubsystem::GetReputationTier(const AActor* Subject, FGameplayTag FactionTag) const
{
	return ResolveTier(GetReputation(Subject, FactionTag));
}

bool UNarr_ReputationSubsystem::MeetsStanding(const AActor* Subject, FGameplayTag FactionTag, int32 MinStanding) const
{
	return GetReputation(Subject, FactionTag) >= static_cast<float>(MinStanding);
}

//~ ISeam_Persistable --------------------------------------------------------------------------

void UNarr_ReputationSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FNarr_ReputationSaveRecord Record;
	Record.FactionTags.Reserve(FactionStandings.Num());
	Record.FactionValues.Reserve(FactionStandings.Num());
	for (const TPair<FGameplayTag, float>& Pair : FactionStandings)
	{
		Record.FactionTags.Add(Pair.Key);
		Record.FactionValues.Add(Pair.Value);
	}
	Record.NpcTags.Reserve(NpcAffinities.Num());
	Record.NpcValues.Reserve(NpcAffinities.Num());
	for (const TPair<FGameplayTag, float>& Pair : NpcAffinities)
	{
		Record.NpcTags.Add(Pair.Key);
		Record.NpcValues.Add(Pair.Value);
	}
	Out = FInstancedStruct::Make(Record);
}

void UNarr_ReputationSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (!HasWorldAuthority())
	{
		return; // clients receive standing via hub replication
	}
	if (!In.IsValid() || In.GetScriptStruct() != FNarr_ReputationSaveRecord::StaticStruct())
	{
		UE_LOG(LogDP, Warning, TEXT("[Narr] Reputation RestoreState: record missing or wrong type; skipping."));
		return;
	}

	const FNarr_ReputationSaveRecord& Record = In.Get<FNarr_ReputationSaveRecord>();
	FactionStandings.Reset();
	NpcAffinities.Reset();

	const int32 FactionNum = FMath::Min(Record.FactionTags.Num(), Record.FactionValues.Num());
	for (int32 i = 0; i < FactionNum; ++i)
	{
		// Route through ApplyStanding so the hub mirror is re-seeded too.
		ApplyStanding(/*bFaction=*/true, Record.FactionTags[i], Record.FactionValues[i]);
	}
	const int32 NpcNum = FMath::Min(Record.NpcTags.Num(), Record.NpcValues.Num());
	for (int32 i = 0; i < NpcNum; ++i)
	{
		ApplyStanding(/*bFaction=*/false, Record.NpcTags[i], Record.NpcValues[i]);
	}

	UE_LOG(LogDP, Log, TEXT("[Narr] Reputation restored: %d factions, %d npcs."), FactionNum, NpcNum);
}

FGameplayTag UNarr_ReputationSubsystem::GetPersistenceKind_Implementation() const
{
	return NarrativeStoryNativeTags::Persist_Narrative_Reputation;
}

//~ Debug --------------------------------------------------------------------------------------

FString UNarr_ReputationSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Reputation: factions=%d npcs=%d authority=%s"),
		FactionStandings.Num(), NpcAffinities.Num(), HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
