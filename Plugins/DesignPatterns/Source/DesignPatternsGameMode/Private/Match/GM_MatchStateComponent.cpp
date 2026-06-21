// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Match/GM_MatchStateComponent.h"

#include "Match/GM_MatchTypes.h"
#include "Ruleset/GM_RulesetDefinition.h"
#include "Score/GM_ScoreSubsystem.h"
#include "Score/GM_ScoreCarrier.h"
#include "Settings/GM_DeveloperSettings.h"
#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

// FInstancedStruct (bus payload). Version-gated — engine moved the header location in 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UGM_MatchStateComponent::UGM_MatchStateComponent()
{
	// The match FSM is the replicated carrier of record (HARD RULE 5: replicated state on a component, never
	// a subsystem). Tick on the authority drives condition evaluation and the round intermission.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	SetIsReplicatedByDefault(true);
}

void UGM_MatchStateComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!HasMatchAuthority())
	{
		// Clients never seed/evaluate; they observe the replicated tag via OnRep.
		return;
	}

	// Seed the initial state to WaitingToStart on the authority. TransitionTo broadcasts and publishes.
	if (!MatchState.IsValid())
	{
		TransitionTo(GameModeNativeTags::Match_WaitingToStart.GetTag());
	}

	// Seed the scoreboard from the effective ruleset's team config so clients can read a populated board
	// from the outset (the score subsystem is authority-guarded internally).
	if (UGM_RulesetDefinition* Effective = ResolveEffectiveRuleset())
	{
		if (UGM_ScoreSubsystem* Score = FDP_SubsystemStatics::GetWorldSubsystem<UGM_ScoreSubsystem>(this))
		{
			Score->SeedFromRuleset(Effective);
		}
	}
}

void UGM_MatchStateComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasMatchAuthority())
	{
		return;
	}

	const UGM_DeveloperSettings* Settings = UGM_DeveloperSettings::Get();

	// --- Auto-start out of WaitingToStart -------------------------------------------------------
	if (MatchState == GameModeNativeTags::Match_WaitingToStart.GetTag())
	{
		// Defensive: default to auto-start when settings are unavailable so a match is not stuck forever.
		const bool bAutoStart = Settings ? Settings->bAutoStartWhenReady : true;
		if (bAutoStart)
		{
			UGM_RulesetDefinition* Ruleset = ResolveEffectiveRuleset();
			// No ruleset => no start gating => start immediately. With a ruleset, ALL start conditions must hold.
			const bool bReady = !Ruleset || Ruleset->AllStartConditionsMet(this);
			if (bReady)
			{
				StartMatch();
			}
		}
		return;
	}

	// --- In-progress: throttle ruleset evaluation to the settings cadence -----------------------
	if (MatchState == GameModeNativeTags::Match_InProgress.GetTag())
	{
		// Defensive cadence fallback: 4 Hz if settings are unavailable (matches the settings default).
		const float Hz = (Settings && Settings->ConditionEvalHz > 0.f) ? Settings->ConditionEvalHz : 4.f;
		const float Interval = 1.f / Hz;

		ConditionEvalAccumulator += DeltaTime;
		if (ConditionEvalAccumulator >= Interval)
		{
			ConditionEvalAccumulator = 0.f;
			EvaluateRuleset();
		}
		return;
	}

	// --- RoundOver intermission countdown -------------------------------------------------------
	if (MatchState == GameModeNativeTags::Match_RoundOver.GetTag())
	{
		if (RoundOverCountdown >= 0.f)
		{
			RoundOverCountdown -= DeltaTime;
			if (RoundOverCountdown <= 0.f)
			{
				RoundOverCountdown = -1.f;
				AdvanceRound();
			}
		}
		return;
	}
}

void UGM_MatchStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGM_MatchStateComponent, MatchState);
	DOREPLIFETIME(UGM_MatchStateComponent, CurrentRound);
	DOREPLIFETIME(UGM_MatchStateComponent, WinningKey);
	DOREPLIFETIME(UGM_MatchStateComponent, InProgressStartServerTime);
	DOREPLIFETIME(UGM_MatchStateComponent, Ruleset);
}

//~ Ruleset ---------------------------------------------------------------------------------------

void UGM_MatchStateComponent::SetRuleset(UGM_RulesetDefinition* InRuleset)
{
	if (!HasMatchAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("GM_MatchStateComponent::SetRuleset rejected on client."));
		return;
	}
	Ruleset = InRuleset;

	// Re-seed the scoreboard from the new ruleset (authority-guarded inside the subsystem).
	if (UGM_ScoreSubsystem* Score = FDP_SubsystemStatics::GetWorldSubsystem<UGM_ScoreSubsystem>(this))
	{
		Score->SeedFromRuleset(InRuleset);
	}
}

UGM_RulesetDefinition* UGM_MatchStateComponent::GetRuleset() const
{
	return ResolveEffectiveRuleset();
}

UGM_RulesetDefinition* UGM_MatchStateComponent::ResolveEffectiveRuleset() const
{
	if (Ruleset)
	{
		return Ruleset;
	}
	// Fall back to the project default ruleset from settings (a soft ptr; loaded synchronously when needed,
	// which is acceptable at match setup time). Empty => documented inert mode (no auto win/lose).
	if (const UGM_DeveloperSettings* Settings = UGM_DeveloperSettings::Get())
	{
		if (!Settings->DefaultRuleset.IsNull())
		{
			return Settings->DefaultRuleset.LoadSynchronous();
		}
	}
	return nullptr;
}

//~ State queries ---------------------------------------------------------------------------------

bool UGM_MatchStateComponent::IsInProgress() const
{
	return MatchState == GameModeNativeTags::Match_InProgress.GetTag();
}

bool UGM_MatchStateComponent::IsMatchOver() const
{
	return MatchState == GameModeNativeTags::Match_MatchOver.GetTag();
}

float UGM_MatchStateComponent::GetElapsedInProgressSeconds() const
{
	if (InProgressStartServerTime < 0.f)
	{
		return 0.f;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	// Elapsed is derived from the replicated server start timestamp against the world clock, so clients get
	// a consistent elapsed time without needing their own timer.
	return FMath::Max(0.f, World->GetTimeSeconds() - InProgressStartServerTime);
}

//~ Authority transitions -------------------------------------------------------------------------

bool UGM_MatchStateComponent::StartMatch()
{
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (MatchState != GameModeNativeTags::Match_WaitingToStart.GetTag())
	{
		return false;
	}

	CurrentRound = 1;
	return TransitionTo(GameModeNativeTags::Match_InProgress.GetTag());
}

bool UGM_MatchStateComponent::EndRound()
{
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (MatchState != GameModeNativeTags::Match_InProgress.GetTag())
	{
		return false;
	}

	return TransitionTo(GameModeNativeTags::Match_RoundOver.GetTag());
}

bool UGM_MatchStateComponent::AdvanceRound()
{
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (MatchState != GameModeNativeTags::Match_RoundOver.GetTag())
	{
		return false;
	}

	const UGM_RulesetDefinition* Effective = ResolveEffectiveRuleset();
	// Clamp round count >= 1 defensively (the ruleset meta clamps it, but a null ruleset means single-round).
	const int32 RoundCount = Effective ? FMath::Max(1, Effective->RoundCount) : 1;

	if (CurrentRound >= RoundCount)
	{
		// No rounds remain: decide the match. EndMatch resolves the leader as the winner per the ruleset's
		// bHighestScoreWins policy when no explicit key is passed.
		return EndMatch(FGameplayTag());
	}

	++CurrentRound;
	return TransitionTo(GameModeNativeTags::Match_InProgress.GetTag());
}

bool UGM_MatchStateComponent::EndMatch(FGameplayTag WinKey)
{
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (MatchState == GameModeNativeTags::Match_MatchOver.GetTag())
	{
		return false;
	}

	// Resolve the winner if the caller did not name one and the ruleset decides by highest score.
	FGameplayTag ResolvedWinner = WinKey;
	if (!ResolvedWinner.IsValid())
	{
		const UGM_RulesetDefinition* Effective = ResolveEffectiveRuleset();
		const bool bHighestWins = Effective ? Effective->bHighestScoreWins : true;
		if (bHighestWins)
		{
			if (const UGM_ScoreSubsystem* Score = FDP_SubsystemStatics::GetWorldSubsystem<UGM_ScoreSubsystem>(this))
			{
				ResolvedWinner = Score->GetLeadingKey();
			}
		}
	}

	WinningKey = ResolvedWinner;

	const bool bChanged = TransitionTo(GameModeNativeTags::Match_MatchOver.GetTag());

	// Finalise results through the score subsystem (locks the carrier's results-final flag and broadcasts
	// the decided message). Authority-guarded inside the subsystem.
	if (UGM_ScoreSubsystem* Score = FDP_SubsystemStatics::GetWorldSubsystem<UGM_ScoreSubsystem>(this))
	{
		Score->FinalizeResults(ResolvedWinner);
	}

	return bChanged;
}

bool UGM_MatchStateComponent::EvaluateConditionsNow()
{
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (MatchState != GameModeNativeTags::Match_InProgress.GetTag())
	{
		return false;
	}
	return EvaluateRuleset();
}

//~ Internals -------------------------------------------------------------------------------------

bool UGM_MatchStateComponent::EvaluateRuleset()
{
	// Authority-only; callers guard, but assert the precondition defensively.
	if (!HasMatchAuthority())
	{
		return false;
	}

	UGM_RulesetDefinition* Effective = ResolveEffectiveRuleset();
	if (!Effective)
	{
		// Inert mode: no ruleset => no automatic win/lose. Manual transitions still work.
		return false;
	}

	// Lose checks first: a lose condition ends the match WITHOUT a winner (a draw/loss). This precedence
	// means a simultaneous win+lose resolves as a loss (the safer outcome for "objective lost" rulesets).
	if (Effective->AnyLoseConditionMet(this))
	{
		return EndMatch(FGameplayTag()); // Empty winner = draw/loss.
	}

	if (Effective->AnyWinConditionMet(this))
	{
		// Win: the leader (or the ruleset's policy) decides the winner. EndMatch resolves the key when empty.
		FGameplayTag Winner;
		if (Effective->bHighestScoreWins)
		{
			if (const UGM_ScoreSubsystem* Score = FDP_SubsystemStatics::GetWorldSubsystem<UGM_ScoreSubsystem>(this))
			{
				Winner = Score->GetLeadingKey();
			}
		}
		return EndMatch(Winner);
	}

	return false;
}

bool UGM_MatchStateComponent::TransitionTo(const FGameplayTag& NewState)
{
	// Authority-only; callers guard.
	if (!HasMatchAuthority())
	{
		return false;
	}
	if (!NewState.IsValid() || MatchState == NewState)
	{
		return false;
	}

	const FGameplayTag OldState = MatchState;
	MatchState = NewState;

	// Per-state bookkeeping (server side; clients mirror via OnRep using the replicated start time).
	if (NewState == GameModeNativeTags::Match_InProgress.GetTag())
	{
		// Seed the in-progress clock so TimeElapsed conditions and the UI countdown have a consistent origin.
		if (const UWorld* World = GetWorld())
		{
			InProgressStartServerTime = World->GetTimeSeconds();
		}
		ConditionEvalAccumulator = 0.f;
	}
	else if (NewState == GameModeNativeTags::Match_RoundOver.GetTag())
	{
		// Stop the in-progress clock; arm the intermission countdown if settings request auto-advance.
		InProgressStartServerTime = -1.f;
		const UGM_DeveloperSettings* Settings = UGM_DeveloperSettings::Get();
		const float Intermission = Settings ? FMath::Max(0.f, Settings->RoundOverIntermissionSeconds) : 0.f;
		RoundOverCountdown = (Intermission > 0.f) ? Intermission : -1.f;
	}
	else
	{
		// WaitingToStart / MatchOver: not in progress.
		InProgressStartServerTime = -1.f;
	}

	// Surface locally on the authority (OnRep handles clients) and publish on the bus.
	OnMatchStateChanged.Broadcast(this, OldState, NewState);
	PublishStateChanged(OldState, NewState);

	UE_LOG(LogDPFSM, Log, TEXT("GM_MatchState: %s -> %s (round %d)"),
		OldState.IsValid() ? *OldState.ToString() : TEXT("(none)"), *NewState.ToString(), CurrentRound);
	return true;
}

void UGM_MatchStateComponent::PublishStateChanged(const FGameplayTag& OldState, const FGameplayTag& NewState) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FGM_MatchStateChangedPayload Payload;
	Payload.OldState = OldState;
	Payload.NewState = NewState;
	Payload.Round = CurrentRound;

	const FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_MatchStateChanged, Wrapped,
		const_cast<UGM_MatchStateComponent*>(this));
}

void UGM_MatchStateComponent::OnRep_MatchState(FGameplayTag OldState)
{
	// Client-side surface of a replicated state change. The authority already broadcast/published locally;
	// here we mirror the local delegate and the bus message so client UIs react in sync.
	OnMatchStateChanged.Broadcast(this, OldState, MatchState);
	PublishStateChanged(OldState, MatchState);

	UE_LOG(LogDPFSM, Verbose, TEXT("GM_MatchState (client): %s -> %s"),
		OldState.IsValid() ? *OldState.ToString() : TEXT("(none)"), *MatchState.ToString());
}

void UGM_MatchStateComponent::OnRep_Ruleset()
{
	// The ruleset arriving on a client is structural-only (data for round/time UI); log for diagnostics.
	UE_LOG(LogDPFSM, Verbose, TEXT("GM_MatchState (client): ruleset replicated (%s)."),
		Ruleset ? *Ruleset->GetName() : TEXT("null"));
}
