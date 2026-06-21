// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Score/GM_ScoreSubsystem.h"

#include "Score/GM_ScoreCarrier.h"
#include "Match/GM_MatchStateComponent.h"
#include "Match/GM_MatchTypes.h"
#include "Ruleset/GM_RulesetDefinition.h"
#include "Settings/GM_DeveloperSettings.h"
#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"

// FInstancedStruct (bus payload). Version-gated — engine moved the header location in 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UGM_ScoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UGM_DeveloperSettings* Settings = UGM_DeveloperSettings::Get();

	// Resolve the default scoreboard bucket once. Defensive: when the settings CDO is somehow null (extreme
	// early-load), fall back to the module's native DP.Score.Default tag rather than an invalid tag.
	DefaultBucket = (Settings && Settings->DefaultScoreBucket.IsValid())
		? Settings->DefaultScoreBucket
		: GameModeNativeTags::Score_Default.GetTag();

	// Eager carrier spawn (per settings) so clients can read a zeroed scoreboard immediately. Only the
	// authority actually spawns; clients pick up the replicated carrier lazily. Defensive: default to eager
	// when settings are unavailable so a zeroed board is still presented.
	const bool bEager = Settings ? Settings->bSpawnScoreCarrierEagerly : true;
	if (bEager && HasWorldAuthority())
	{
		GetOrSpawnCarrier();
	}
}

void UGM_ScoreSubsystem::Deinitialize()
{
	// Drop the carrier's service registration if it still points at our carrier (a later world may have
	// overridden it). The carrier itself is world-owned and torn down with the world.
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		AGM_ScoreCarrier* Carrier = CarrierWeak.Get();
		if (Carrier && Locator->ResolveService(GameModeNativeTags::Service_GM_Score) == Carrier)
		{
			Locator->UnregisterService(GameModeNativeTags::Service_GM_Score);
		}
	}

	CarrierWeak.Reset();
	Super::Deinitialize();
}

//~ Carrier lifecycle -----------------------------------------------------------------------------

AGM_ScoreCarrier* UGM_ScoreSubsystem::GetOrSpawnCarrier()
{
	// Already have a live one?
	if (AGM_ScoreCarrier* Existing = CarrierWeak.Get())
	{
		return Existing;
	}

	// A replicated carrier may already exist in the world (client picking up the server's, or a re-resolve
	// after the weak handle dropped). Adopt it before spawning a new one.
	if (AGM_ScoreCarrier* Found = FindCarrierInWorld())
	{
		CarrierWeak = Found;
		RegisterCarrierService(Found);
		return Found;
	}

	// Otherwise spawn on the authority only.
	return SpawnCarrierIfAuthority();
}

AGM_ScoreCarrier* UGM_ScoreSubsystem::GetCarrier() const
{
	if (AGM_ScoreCarrier* Existing = CarrierWeak.Get())
	{
		return Existing;
	}
	// Client-safe re-resolve without spawning.
	return FindCarrierInWorld();
}

AGM_ScoreCarrier* UGM_ScoreSubsystem::SpawnCarrierIfAuthority()
{
	UWorld* World = GetWorld();
	if (!World || !HasWorldAuthority())
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // Match state never persists across a save; it is per-session.

	AGM_ScoreCarrier* Carrier = World->SpawnActor<AGM_ScoreCarrier>(AGM_ScoreCarrier::StaticClass(), Params);
	if (!Carrier)
	{
		UE_LOG(LogDP, Warning, TEXT("GM_ScoreSubsystem: failed to spawn score carrier."));
		return nullptr;
	}

	CarrierWeak = Carrier;
	RegisterCarrierService(Carrier);
	UE_LOG(LogDP, Log, TEXT("GM_ScoreSubsystem: spawned score carrier %s."), *Carrier->GetName());
	return Carrier;
}

AGM_ScoreCarrier* UGM_ScoreSubsystem::FindCarrierInWorld() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AGM_ScoreCarrier> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

void UGM_ScoreSubsystem::RegisterCarrierService(AGM_ScoreCarrier* Carrier)
{
	if (!Carrier)
	{
		return;
	}
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return;
	}

	// Register the carrier's ISeam_ScoreSource under DP.Service.GM.Score as WeakObserved: the locator is
	// GameInstance-scoped and must NOT hold a hard ref to a world-lifetime actor (HARD RULE 3). Both server
	// and client register their local carrier so conditions/UI on either side resolve a live read seam.
	Locator->RegisterService(GameModeNativeTags::Service_GM_Score, Carrier,
		EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	UE_LOG(LogDPService, Log, TEXT("GM_ScoreSubsystem: registered ISeam_ScoreSource under %s."),
		*GameModeNativeTags::Service_GM_Score.GetTag().ToString());
}

//~ Match wiring ----------------------------------------------------------------------------------

void UGM_ScoreSubsystem::SeedFromRuleset(UGM_RulesetDefinition* Ruleset)
{
	// HARD RULE 5: authority guard at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}

	AGM_ScoreCarrier* Carrier = GetOrSpawnCarrier();
	if (!Carrier)
	{
		return;
	}

	if (!Ruleset)
	{
		// Null ruleset seeds nothing; ad-hoc buckets are created on first score. Documented inert path.
		UE_LOG(LogDP, Verbose, TEXT("GM_ScoreSubsystem::SeedFromRuleset called with null ruleset; no buckets seeded."));
		return;
	}

	for (const FGM_TeamConfig& Team : Ruleset->Teams)
	{
		if (Team.TeamTag.IsValid())
		{
			Carrier->EnsureBucket(Team.TeamTag, Team.StartingScore, Team.DisplayName);
		}
	}
}

void UGM_ScoreSubsystem::FinalizeResults(FGameplayTag WinningKey)
{
	if (!HasWorldAuthority())
	{
		return;
	}

	AGM_ScoreCarrier* Carrier = GetOrSpawnCarrier();
	if (!Carrier)
	{
		return;
	}

	Carrier->SetResultsFinal(true);

	// Broadcast the decided message so game-flow advances to a results phase and analytics records the
	// outcome — all without depending on this module's concrete types.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FGM_MatchDecidedPayload Payload;
		Payload.WinningKey = WinningKey;
		Payload.WinningScore = WinningKey.IsValid() ? Carrier->GetScore_Implementation(WinningKey) : 0;

		const FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
		Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_MatchDecided, Wrapped, this);
	}

	UE_LOG(LogDP, Log, TEXT("GM_ScoreSubsystem: results finalised (winner=%s)."),
		WinningKey.IsValid() ? *WinningKey.ToString() : TEXT("draw"));
}

//~ Authority score mutators (each early-returns on clients) --------------------------------------

int64 UGM_ScoreSubsystem::AddScore(FGameplayTag Key, int64 Delta)
{
	if (!HasWorldAuthority())
	{
		return 0;
	}

	AGM_ScoreCarrier* Carrier = GetOrSpawnCarrier();
	if (!Carrier)
	{
		return 0;
	}

	const FGameplayTag NormalizedKey = NormalizeKey(Key);
	const int64 NewScore = Carrier->AddScore(NormalizedKey, Delta);

	PublishScoreChanged(NormalizedKey, NewScore, Delta);

	// A scoring play that crosses a win threshold should end the match at once, not on the next cadence tick.
	if (UGM_MatchStateComponent* MatchComp = FindMatchComponent())
	{
		MatchComp->EvaluateConditionsNow();
	}

	return NewScore;
}

int64 UGM_ScoreSubsystem::SetScore(FGameplayTag Key, int64 NewScore)
{
	if (!HasWorldAuthority())
	{
		return 0;
	}

	AGM_ScoreCarrier* Carrier = GetOrSpawnCarrier();
	if (!Carrier)
	{
		return 0;
	}

	const FGameplayTag NormalizedKey = NormalizeKey(Key);
	const int64 Previous = Carrier->GetScore_Implementation(NormalizedKey);
	const int64 Applied = Carrier->SetScore(NormalizedKey, NewScore);

	PublishScoreChanged(NormalizedKey, Applied, Applied - Previous);

	if (UGM_MatchStateComponent* MatchComp = FindMatchComponent())
	{
		MatchComp->EvaluateConditionsNow();
	}

	return Applied;
}

void UGM_ScoreSubsystem::ResetScores()
{
	if (!HasWorldAuthority())
	{
		return;
	}
	if (AGM_ScoreCarrier* Carrier = GetCarrier())
	{
		Carrier->ResetScores();
	}
}

//~ Reads (client-safe) ---------------------------------------------------------------------------

int64 UGM_ScoreSubsystem::GetScore(FGameplayTag Key) const
{
	if (const AGM_ScoreCarrier* Carrier = GetCarrier())
	{
		return Carrier->GetScore_Implementation(NormalizeKey(Key));
	}
	return 0;
}

FGameplayTag UGM_ScoreSubsystem::GetLeadingKey() const
{
	if (const AGM_ScoreCarrier* Carrier = GetCarrier())
	{
		return Carrier->GetLeadingKey();
	}
	return FGameplayTag();
}

//~ UDP_WorldSubsystem ----------------------------------------------------------------------------

FString UGM_ScoreSubsystem::GetDPDebugString_Implementation() const
{
	const AGM_ScoreCarrier* Carrier = GetCarrier();
	const int32 Buckets = Carrier ? Carrier->GetRows().Num() : 0;
	const FGameplayTag Leader = GetLeadingKey();
	return FString::Printf(TEXT("Score: auth=%s carrier=%s buckets=%d leader=%s"),
		HasWorldAuthority() ? TEXT("Y") : TEXT("N"),
		Carrier ? TEXT("Y") : TEXT("N"),
		Buckets,
		Leader.IsValid() ? *Leader.ToString() : TEXT("(none)"));
}

//~ Internals -------------------------------------------------------------------------------------

UDP_ServiceLocatorSubsystem* UGM_ScoreSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

UGM_MatchStateComponent* UGM_ScoreSubsystem::FindMatchComponent() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (const AGameStateBase* GS = World->GetGameState())
	{
		return GS->FindComponentByClass<UGM_MatchStateComponent>();
	}
	return nullptr;
}

FGameplayTag UGM_ScoreSubsystem::NormalizeKey(const FGameplayTag& Key) const
{
	if (Key.IsValid())
	{
		return Key;
	}
	// Fall back to the configured default bucket (resolved at init), then the native default as a last resort.
	return DefaultBucket.IsValid() ? DefaultBucket : GameModeNativeTags::Score_Default.GetTag();
}

void UGM_ScoreSubsystem::PublishScoreChanged(const FGameplayTag& Key, int64 NewScore, int64 Delta) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FGM_ScoreChangedPayload Payload;
	Payload.Key = Key;
	Payload.NewScore = NewScore;
	Payload.Delta = Delta;

	const FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_ScoreChanged, Wrapped, const_cast<UGM_ScoreSubsystem*>(this));
}
