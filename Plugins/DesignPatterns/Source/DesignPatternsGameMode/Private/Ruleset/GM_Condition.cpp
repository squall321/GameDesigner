// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Ruleset/GM_Condition.h"

#include "Ruleset/GM_RulesetDefinition.h"
#include "Match/GM_MatchStateComponent.h"
#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"

// Score read seam (the replicated carrier implements it).
#include "Score/Seam_ScoreSource.h"
// Team affinity seam (used by the last-team-standing condition).
#include "Identity/Seam_TeamAffinity.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"

// The world-hub query seam is in a sibling Wave-2 module; only the SEAM interface is included (never a
// concrete hub type), and it is resolved at runtime through the service locator so an absent hub degrades
// to the condition's documented default.
#include "Query/WorldHub_Queryable.h"
#include "Hub/WorldHub_Scope.h"
#include "WorldHub_NativeTags.h"

#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "GM_Condition"

//================================================================================================
// UGM_Condition (base)
//================================================================================================

bool UGM_Condition::Evaluate_Implementation(UObject* /*WorldContext*/) const
{
	// The abstract base holds no predicate; a never-firing condition is the safe default so a designer
	// who leaves a blank slot does not accidentally end the match.
	return false;
}

FText UGM_Condition::GetConditionDescription_Implementation() const
{
	return LOCTEXT("BaseCondition", "Condition");
}

TScriptInterface<ISeam_ScoreSource> UGM_Condition::ResolveScoreSource(const UObject* WorldContext) const
{
	if (!WorldContext)
	{
		return nullptr;
	}

	// Score is read through the seam registered by the carrier under DP.Service.GM.Score, so a condition
	// never hard-depends on the concrete carrier/subsystem (HARD RULE 9).
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext))
	{
		if (UObject* Provider = Locator->ResolveService(GameModeNativeTags::Service_GM_Score))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_ScoreSource::StaticClass()))
			{
				TScriptInterface<ISeam_ScoreSource> Source;
				Source.SetObject(Provider);
				Source.SetInterface(Cast<ISeam_ScoreSource>(Provider));
				return Source;
			}
		}
	}
	return nullptr;
}

//================================================================================================
// UGM_Condition_ScoreAtLeast
//================================================================================================

bool UGM_Condition_ScoreAtLeast::Evaluate_Implementation(UObject* WorldContext) const
{
	const TScriptInterface<ISeam_ScoreSource> Source = ResolveScoreSource(WorldContext);
	if (!Source)
	{
		// No score seam resolved: degrade inert (the threshold can never be met), so the match does not
		// end spuriously when scoring is not wired up yet.
		return false;
	}

	if (bAnyBucket)
	{
		// "Any team reaching N" — test the highest score across the whole board.
		TArray<FSeam_ScoreRow> Rows;
		ISeam_ScoreSource::Execute_GetAllScores(Source.GetObject(), Rows);
		for (const FSeam_ScoreRow& Row : Rows)
		{
			if (Row.Score >= Threshold)
			{
				return true;
			}
		}
		return false;
	}

	// Single-bucket test. An invalid ScoreKey falls back to the neutral default bucket so a designer who
	// leaves it blank still gets a meaningful read rather than a guaranteed-zero one.
	FGameplayTag Key = ScoreKey;
	if (!Key.IsValid())
	{
		Key = GameModeNativeTags::Score_Default;
	}

	const int64 Current = ISeam_ScoreSource::Execute_GetScore(Source.GetObject(), Key);
	return Current >= Threshold;
}

FText UGM_Condition_ScoreAtLeast::GetConditionDescription_Implementation() const
{
	if (bAnyBucket)
	{
		return FText::Format(LOCTEXT("ScoreAnyBucketDesc", "Any team's score reaches {0}"),
			FText::AsNumber(Threshold));
	}
	const FString KeyStr = ScoreKey.IsValid() ? ScoreKey.ToString() : TEXT("(default)");
	return FText::Format(LOCTEXT("ScoreBucketDesc", "Score for {0} reaches {1}"),
		FText::FromString(KeyStr), FText::AsNumber(Threshold));
}

//================================================================================================
// UGM_Condition_TimeElapsed
//================================================================================================

bool UGM_Condition_TimeElapsed::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!WorldContext)
	{
		return false;
	}

	// Elapsed in-progress time is owned by the match component (it seeds the reference clock on entering
	// InProgress and replicates the server start time), keeping THIS condition stateless. Resolve the
	// component from the world's GameState; this condition is a sibling-of-the-component read.
	const UGM_MatchStateComponent* MatchComp = nullptr;
	if (const UWorld* World = WorldContext->GetWorld())
	{
		if (const AGameStateBase* GS = World->GetGameState())
		{
			MatchComp = GS->FindComponentByClass<UGM_MatchStateComponent>();
		}
	}
	if (!MatchComp)
	{
		// Without a match component there is no notion of elapsed time; degrade inert.
		return false;
	}

	const float Elapsed = MatchComp->GetElapsedInProgressSeconds();

	// A configured 0 means "use the ruleset's TimeLimitSeconds" so the limit lives in one place. Defensive
	// non-negative clamp on both the per-condition and the ruleset value.
	float Limit = FMath::Max(0.f, Seconds);
	if (Limit <= 0.f)
	{
		if (const UGM_RulesetDefinition* Ruleset = MatchComp->GetRuleset())
		{
			Limit = FMath::Max(0.f, Ruleset->TimeLimitSeconds);
		}
	}

	// A non-positive effective limit means "no time limit"; never fire in that case.
	if (Limit <= 0.f)
	{
		return false;
	}

	return Elapsed >= Limit;
}

FText UGM_Condition_TimeElapsed::GetConditionDescription_Implementation() const
{
	if (Seconds <= 0.f)
	{
		return LOCTEXT("TimeElapsedRulesetDesc", "Round time limit elapsed");
	}
	return FText::Format(LOCTEXT("TimeElapsedDesc", "{0} seconds elapsed"), FText::AsNumber(Seconds));
}

//================================================================================================
// UGM_Condition_LastTeamStanding
//================================================================================================

bool UGM_Condition_LastTeamStanding::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!WorldContext)
	{
		return false;
	}

	const UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return false;
	}

	// Resolve the team-affinity seam from the locator (the GameMode team subsystem publishes it). Absent
	// seam -> degrade to false (documented inert) so a missing team system never ends the match.
	TScriptInterface<ISeam_TeamAffinity> TeamSeam;
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext))
	{
		if (UObject* Provider = Locator->ResolveService(GameModeNativeTags::Service_GM_TeamAffinity))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_TeamAffinity::StaticClass()))
			{
				TeamSeam.SetObject(Provider);
				TeamSeam.SetInterface(Cast<ISeam_TeamAffinity>(Provider));
			}
		}
	}
	if (!TeamSeam)
	{
		return false;
	}

	// Optional class filter so only meaningful actors (e.g. pawns) count toward team presence; an unset
	// filter counts every team-tagged actor. The soft class is resolved synchronously here because this is
	// authority-side, infrequent (gated by the eval cadence) and the filter class is normally already loaded.
	UClass* Filter = nullptr;
	if (!CountableClass.IsNull())
	{
		Filter = CountableClass.LoadSynchronous();
	}

	// Count DISTINCT valid team tags among relevant actors. FFA (each actor its own team) and team modes
	// both work because membership is policy-driven behind the seam.
	TSet<FGameplayTag> LiveTeams;
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (Filter && !Actor->IsA(Filter))
		{
			continue;
		}
		const FGameplayTag Team = ISeam_TeamAffinity::Execute_GetTeamTag(TeamSeam.GetObject(), Actor);
		if (Team.IsValid())
		{
			LiveTeams.Add(Team);
		}
	}

	const int32 Remaining = LiveTeams.Num();
	// bAllowSingleSurvivor: <= 1 team remaining ends the match (the classic "last team standing"); when
	// false, the match only ends on a total wipe (0 teams remaining), e.g. a co-op all-down loss check.
	return bAllowSingleSurvivor ? (Remaining <= 1) : (Remaining == 0);
}

FText UGM_Condition_LastTeamStanding::GetConditionDescription_Implementation() const
{
	return bAllowSingleSurvivor
		? LOCTEXT("LastTeamDesc", "One or fewer teams remain")
		: LOCTEXT("AllTeamsDownDesc", "No teams remain");
}

//================================================================================================
// UGM_Condition_HubFlag
//================================================================================================

bool UGM_Condition_HubFlag::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!WorldContext || !FlagKey.IsValid())
	{
		return bResultIfMissing;
	}

	// Resolve the world-hub read seam from the locator. The World module registers its IWorldHub_Queryable
	// provider there; we depend only on the SEAM (never a concrete hub class), so an absent hub degrades to
	// the designer-chosen bResultIfMissing (fail-open vs fail-closed).
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext);
	if (!Locator)
	{
		return bResultIfMissing;
	}

	// The hub subsystem (which implements IWorldHub_Queryable) registers under DP.Service.WorldHub; a
	// dedicated read-only adapter may instead register under DP.Service.WorldHub.Query. Try the dedicated
	// query key first, then the subsystem key, so either wiring resolves.
	IWorldHub_Queryable* Hub = nullptr;
	if (UObject* QueryProvider = Locator->ResolveService(WorldHubNativeTags::Service_WorldHub_Query))
	{
		Hub = Cast<IWorldHub_Queryable>(QueryProvider);
	}
	if (!Hub)
	{
		if (UObject* Provider = Locator->ResolveService(WorldHubNativeTags::Service_WorldHub))
		{
			Hub = Cast<IWorldHub_Queryable>(Provider);
		}
	}
	if (!Hub)
	{
		return bResultIfMissing;
	}

	// Flags are read at the Global scope: a match-outcome gate is a world-level fact, not entity/faction
	// owned. The hub applies its own fallback internally; HasValue treats a definition default as "absent"
	// so a flag nobody has set yet counts as missing (per the seam contract).
	const FWorldHub_Scope Scope = FWorldHub_Scope::Global();
	if (!Hub->HasValue(FlagKey, Scope))
	{
		return bResultIfMissing;
	}

	FInstancedStruct Value;
	if (!Hub->QueryValue(FlagKey, Scope, Value))
	{
		return bResultIfMissing;
	}

	// The hub stores typed values; we interpret a bool-shaped value. The common world-hub bool wrapper is a
	// single-bool struct, so we read the first bool property generically rather than hard-including a concrete
	// payload type from the World module (which would violate the no-sibling-concrete-include rule).
	const bool bFlagValue = ReadBoolFromInstancedStruct(Value);
	return bFlagValue == bExpectedValue;
}

bool UGM_Condition_HubFlag::ReadBoolFromInstancedStruct(const FInstancedStruct& Value)
{
	const UScriptStruct* ScriptStruct = Value.GetScriptStruct();
	const uint8* Memory = Value.GetMemory();
	if (!ScriptStruct || !Memory)
	{
		return false;
	}

	// Find the first bool property on the value struct and read it. This keeps the condition decoupled from
	// the World module's concrete bool-flag payload type while still reading a boolean world flag.
	for (TFieldIterator<FBoolProperty> It(ScriptStruct); It; ++It)
	{
		const FBoolProperty* BoolProp = *It;
		return BoolProp->GetPropertyValue_InContainer(Memory);
	}

	// No bool field: treat a non-empty value as "true" (a present non-bool flag reads as set).
	return true;
}

FText UGM_Condition_HubFlag::GetConditionDescription_Implementation() const
{
	const FString KeyStr = FlagKey.IsValid() ? FlagKey.ToString() : TEXT("(unset)");
	return FText::Format(LOCTEXT("HubFlagDesc", "World flag {0} is {1}"),
		FText::FromString(KeyStr),
		bExpectedValue ? LOCTEXT("True", "true") : LOCTEXT("False", "false"));
}

#undef LOCTEXT_NAMESPACE
