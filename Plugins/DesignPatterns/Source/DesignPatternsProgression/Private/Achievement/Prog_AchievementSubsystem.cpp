// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Achievement/Prog_AchievementSubsystem.h"

#include "Achievement/Prog_AchievementDefinition.h"
#include "Achievement/Prog_Condition.h"
#include "Settings/Prog_DeveloperSettings.h"
#include "DesignPatternsProgressionModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"

#include "Economy/Seam_Wallet.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Net/Seam_NetValue.h"
#include "Platform/Seam_PlatformAchievements.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "UObject/ScriptInterface.h"

// FInstancedStruct: StructUtils on 5.3/5.4, CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#define LOCTEXT_NAMESPACE "Prog_AchievementSubsystem"

//~ Lifecycle ----------------------------------------------------------------------------------

void UProg_AchievementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Build the definition set lazily on first event so we don't force a data-registry scan during
	// GameInstance init (the registry may not be ready yet at this point in startup ordering).
	bDefinitionsBuilt = false;

	// Subscribe to the module's own bus root + project trigger channels. The bus is also a GI subsystem
	// and is created in the same collection, so it is available here.
	SubscribeToTriggers();

	UE_LOG(LogDP, Log, TEXT("[Prog] AchievementSubsystem initialized."));
}

void UProg_AchievementSubsystem::Deinitialize()
{
	// Drop every bus listener owned by us so no stale callback fires after teardown.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluationTimerHandle);
	}

	Definitions.Empty();
	Flags.Empty();
	Counters.Empty();
	ChannelHits.Empty();
	Unlocked.Empty();
	LastReportedProgress.Empty();

	Super::Deinitialize();
}

//~ Authority ----------------------------------------------------------------------------------

bool UProg_AchievementSubsystem::HasHostAuthority() const
{
	// A GameInstance subsystem has no intrinsic net role. Derive host-ness from the current world's net
	// mode: the host is anything that is NOT a pure client. With no world yet (single-player / early
	// load) we default to "authority" so a single-player RestoreState is never suppressed.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			return World->GetNetMode() != NM_Client;
		}
	}
	return true;
}

//~ Definition set -----------------------------------------------------------------------------

void UProg_AchievementSubsystem::EnsureDefinitionsBuilt()
{
	if (bDefinitionsBuilt)
	{
		return;
	}

	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	if (!Registry)
	{
		// No registry available yet — leave unbuilt so we retry on the next event. Inert default: an
		// empty achievement set (nothing unlocks), never a crash.
		UE_LOG(LogDP, Verbose, TEXT("[Prog] AchievementSubsystem: data registry unavailable; deferring build."));
		return;
	}

	Definitions.Reset();

	// Enumerate every indexed DataTag and keep those that resolve to an achievement definition. FindByTag
	// loads the package synchronously, which is acceptable: achievement definitions are tiny and we only
	// touch them once per session (then cache TObjectPtrs that keep them resident).
	const TArray<FGameplayTag> AllTags = Registry->ListTags();
	for (const FGameplayTag& Tag : AllTags)
	{
		if (UProg_AchievementDefinition* Def = Registry->Find<UProg_AchievementDefinition>(Tag))
		{
			const FGameplayTag Id = Def->DataTag.IsValid() ? Def->DataTag : Tag;
			Definitions.Add(Id, Def);
		}
	}

	bDefinitionsBuilt = true;
	UE_LOG(LogDP, Log, TEXT("[Prog] AchievementSubsystem: built %d achievement definition(s)."),
		Definitions.Num());
}

//~ Bus subscription ---------------------------------------------------------------------------

UDP_MessageBusSubsystem* UProg_AchievementSubsystem::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

void UProg_AchievementSubsystem::SubscribeToTriggers()
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Prog] AchievementSubsystem: message bus unavailable; no triggers bound."));
		return;
	}

	// Keep idempotent: clear any prior subscriptions we own before (re)binding.
	Bus->StopListeningForOwner(this);

	TWeakObjectPtr<UProg_AchievementSubsystem> WeakThis(this);
	auto Handler = [WeakThis](const FDP_Message& Message)
	{
		if (UProg_AchievementSubsystem* Self = WeakThis.Get())
		{
			Self->HandleBusMessage(Message);
		}
	};

	// Always listen to our own DP.Bus.Prog root (child-matching), which covers BalanceChanged / progress
	// traffic and lets achievements react to wallet activity out of the box.
	Bus->ListenNative(ProgTags::Bus, Handler, this, EDP_MessageMatch::ExactOrChild);

	// Plus every project-configured extra trigger channel.
	if (const UProg_DeveloperSettings* Settings = UProg_DeveloperSettings::Get())
	{
		for (const FGameplayTag& Channel : Settings->AchievementTriggerChannels)
		{
			if (Channel.IsValid())
			{
				Bus->ListenNative(Channel, Handler, this, EDP_MessageMatch::ExactOrChild);
			}
		}
	}
}

void UProg_AchievementSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	// Every observed message bumps the raw per-channel occurrence counter that powers BusCounter
	// conditions. Keying is exact: a BusCounter on DP.Bus.Combat.Killed counts only that exact channel,
	// even though we subscribed with child-matching (so the same listener serves many channels).
	if (Message.Channel.IsValid())
	{
		int64& Hits = ChannelHits.FindOrAdd(Message.Channel);
		++Hits;
	}

	// Fold any flag/counter semantics the project drives via this subsystem's API are handled in
	// SetHubFlag/AddHubCounter; raw bus payloads stay opaque here by design (HARD RULE 9: we do not
	// hard-depend on sibling-module payload types). Schedule a coalesced re-evaluation.
	ScheduleEvaluation();
}

//~ Accumulator reads --------------------------------------------------------------------------

bool UProg_AchievementSubsystem::GetHubFlag(const FGameplayTag& FlagTag) const
{
	return FlagTag.IsValid() && Flags.Contains(FlagTag);
}

int64 UProg_AchievementSubsystem::GetHubCounter(const FGameplayTag& CounterTag) const
{
	if (const int64* Value = Counters.Find(CounterTag))
	{
		return *Value;
	}
	return 0;
}

int64 UProg_AchievementSubsystem::GetChannelHitCount(const FGameplayTag& ChannelTag) const
{
	if (const int64* Value = ChannelHits.Find(ChannelTag))
	{
		return *Value;
	}
	return 0;
}

//~ Accumulator writes -------------------------------------------------------------------------

void UProg_AchievementSubsystem::SetHubFlag(FGameplayTag FlagTag, bool bValue)
{
	if (!FlagTag.IsValid())
	{
		return;
	}

	bool bChanged = false;
	if (bValue)
	{
		bool bAlreadySet = false;
		Flags.Add(FlagTag, &bAlreadySet);
		bChanged = !bAlreadySet;
	}
	else
	{
		bChanged = (Flags.Remove(FlagTag) > 0);
	}

	if (bChanged)
	{
		ScheduleEvaluation();
	}
}

void UProg_AchievementSubsystem::AddHubCounter(FGameplayTag CounterTag, int64 Delta)
{
	if (!CounterTag.IsValid() || Delta == 0)
	{
		return;
	}

	int64& Value = Counters.FindOrAdd(CounterTag);
	// Counters are monotonic-ish but never negative; a negative delta clamps at zero rather than wrapping.
	Value = FMath::Max<int64>(0, Value + Delta);
	ScheduleEvaluation();
}

//~ Queries ------------------------------------------------------------------------------------

bool UProg_AchievementSubsystem::IsUnlocked(FGameplayTag Achievement) const
{
	return Achievement.IsValid() && Unlocked.Contains(Achievement);
}

TArray<FGameplayTag> UProg_AchievementSubsystem::GetUnlockedAchievements() const
{
	return Unlocked.Array();
}

float UProg_AchievementSubsystem::GetAchievementProgress(FGameplayTag Achievement) const
{
	if (Unlocked.Contains(Achievement))
	{
		return 1.f;
	}
	if (const float* Value = LastReportedProgress.Find(Achievement))
	{
		return *Value;
	}
	return 0.f;
}

//~ Evaluation ---------------------------------------------------------------------------------

void UProg_AchievementSubsystem::ScheduleEvaluation()
{
	EnsureDefinitionsBuilt();

	const UWorld* World = GetWorld();
	if (!World)
	{
		// No world (e.g. running headless during early init): evaluate synchronously so explicit
		// API-driven unlocks still resolve.
		RunEvaluation();
		return;
	}

	if (bEvaluationScheduled)
	{
		return;
	}

	float Interval = 0.25f; // documented shipped default if the CDO is somehow unavailable
	if (const UProg_DeveloperSettings* Settings = UProg_DeveloperSettings::Get())
	{
		Interval = FMath::Max(0.f, Settings->MinEvaluationIntervalSeconds);
	}

	if (Interval <= 0.f)
	{
		// No coalescing requested: evaluate immediately.
		RunEvaluation();
		return;
	}

	// Coalesce: if enough time has passed since the last pass, evaluate now; otherwise defer the remainder
	// of the interval so a burst of events collapses into a single pass.
	const double Now = World->GetTimeSeconds();
	const double Elapsed = Now - LastEvaluationTime;
	if (Elapsed >= Interval)
	{
		RunEvaluation();
		return;
	}

	const float Remaining = static_cast<float>(Interval - Elapsed);
	bEvaluationScheduled = true;
	World->GetTimerManager().SetTimer(
		EvaluationTimerHandle,
		FTimerDelegate::CreateUObject(this, &UProg_AchievementSubsystem::RunEvaluation),
		Remaining, /*bLoop*/ false);
}

void UProg_AchievementSubsystem::EvaluateNow()
{
	EnsureDefinitionsBuilt();
	RunEvaluation();
}

void UProg_AchievementSubsystem::RunEvaluation()
{
	bEvaluationScheduled = false;
	if (const UWorld* World = GetWorld())
	{
		LastEvaluationTime = World->GetTimeSeconds();
	}

	// Snapshot the values so an unlock side effect (which can broadcast on the bus and re-enter our
	// handler) cannot mutate the map we are iterating.
	TArray<TObjectPtr<UProg_AchievementDefinition>> ToCheck;
	ToCheck.Reserve(Definitions.Num());
	for (const TPair<FGameplayTag, TObjectPtr<UProg_AchievementDefinition>>& Pair : Definitions)
	{
		if (Pair.Value && !Unlocked.Contains(Pair.Key))
		{
			ToCheck.Add(Pair.Value);
		}
	}

	for (const TObjectPtr<UProg_AchievementDefinition>& Def : ToCheck)
	{
		if (Def)
		{
			EvaluateDefinition(*Def);
		}
	}
}

void UProg_AchievementSubsystem::EvaluateDefinition(UProg_AchievementDefinition& Def)
{
	const FGameplayTag Id = Def.DataTag;
	if (!Id.IsValid() || Unlocked.Contains(Id))
	{
		return;
	}

	// An achievement with no conditions never auto-unlocks (the editor validation warns about this);
	// it must be unlocked explicitly via ForceUnlock.
	if (Def.Conditions.Num() == 0)
	{
		return;
	}

	// Strategy AND: every non-null condition must evaluate true.
	bool bAllPass = true;
	for (const TObjectPtr<UProg_Condition>& Condition : Def.Conditions)
	{
		if (!Condition)
		{
			// A null entry is a data error; treat it as unsatisfiable so a broken definition never
			// auto-unlocks.
			bAllPass = false;
			break;
		}
		if (!Condition->Evaluate(this))
		{
			bAllPass = false;
			break;
		}
	}

	if (bAllPass)
	{
		DoUnlock(Def);
	}
	else
	{
		// Not yet unlocked: surface incremental progress so progress UIs / progressive trophies update.
		UpdateProgress(Def);
	}
}

void UProg_AchievementSubsystem::UpdateProgress(UProg_AchievementDefinition& Def)
{
	const FGameplayTag Id = Def.DataTag;
	if (!Id.IsValid() || Def.Conditions.Num() == 0)
	{
		return;
	}

	// Aggregate progress is the mean of each condition's fraction (an AND of strategies is "done" when
	// all are full, so averaging gives a smooth, monotone-ish bar).
	float Sum = 0.f;
	int32 Count = 0;
	for (const TObjectPtr<UProg_Condition>& Condition : Def.Conditions)
	{
		if (Condition)
		{
			Sum += FMath::Clamp(Condition->GetProgressFraction(this), 0.f, 1.f);
			++Count;
		}
	}
	const float Progress = (Count > 0) ? (Sum / static_cast<float>(Count)) : 0.f;

	const float* Last = LastReportedProgress.Find(Id);
	if (Last && FMath::IsNearlyEqual(*Last, Progress, KINDA_SMALL_NUMBER))
	{
		return; // no meaningful change; don't spam delegates/bus
	}
	LastReportedProgress.Add(Id, Progress);

	OnAchievementProgress.Broadcast(Id, Progress);

	FProg_AchievementEvent Event;
	Event.Achievement = Id;
	Event.Progress = Progress;
	Event.bUnlocked = false;
	BroadcastEvent(ProgTags::Bus_AchievementProgress, Event);

	ReportPlatformProgress(Def, Progress);
}

//~ Unlock side effects ------------------------------------------------------------------------

bool UProg_AchievementSubsystem::ForceUnlock(FGameplayTag Achievement)
{
	EnsureDefinitionsBuilt();
	if (!Achievement.IsValid() || Unlocked.Contains(Achievement))
	{
		return false;
	}

	const TObjectPtr<UProg_AchievementDefinition>* Def = Definitions.Find(Achievement);
	if (!Def || !*Def)
	{
		UE_LOG(LogDP, Warning, TEXT("[Prog] ForceUnlock: unknown achievement '%s'."), *Achievement.ToString());
		return false;
	}

	DoUnlock(**Def);
	return true;
}

void UProg_AchievementSubsystem::DoUnlock(UProg_AchievementDefinition& Def)
{
	const FGameplayTag Id = Def.DataTag;
	if (!Id.IsValid())
	{
		return;
	}

	bool bAlreadyUnlocked = false;
	Unlocked.Add(Id, &bAlreadyUnlocked);
	if (bAlreadyUnlocked)
	{
		return; // already unlocked; idempotent
	}

	LastReportedProgress.Add(Id, 1.f);

	UE_LOG(LogDP, Log, TEXT("[Prog] Achievement unlocked: %s"), *Id.ToString());

	// 1) Local delegates.
	OnAchievementUnlocked.Broadcast(Id);
	OnAchievementProgress.Broadcast(Id, 1.f);

	// 2) Bus event (decoupled HUD / SFX).
	FProg_AchievementEvent Event;
	Event.Achievement = Id;
	Event.Progress = 1.f;
	Event.bUnlocked = true;
	BroadcastEvent(ProgTags::Bus_AchievementUnlocked, Event);

	// 3) Analytics (weak seam).
	RecordUnlockAnalytics(Def);

	// 4) Platform trophy (optional seam).
	ReportPlatformUnlock(Def);

	// 5) Currency reward (wallet seam; authority-guarded inside the wallet).
	GrantReward(Def);
}

void UProg_AchievementSubsystem::RecordUnlockAnalytics(const UProg_AchievementDefinition& Def) const
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* Provider = Locator->ResolveService(ProgTags::Service_Analytics);
	if (!Provider || !Provider->Implements<USeam_AnalyticsSink>())
	{
		return; // inert default: no analytics sink bound
	}

	// Gate on the sink's own readiness so we don't record before consent/backend is up.
	if (!ISeam_AnalyticsSink::Execute_IsSinkReady(Provider))
	{
		return;
	}

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Add(FSeam_AnalyticsAttr(FName(TEXT("Achievement")), FSeam_NetValue::MakeTag(Def.DataTag)));
	if (Def.HasReward())
	{
		Attrs.Add(FSeam_AnalyticsAttr(FName(TEXT("RewardCurrency")), FSeam_NetValue::MakeTag(Def.RewardCurrency)));
		Attrs.Add(FSeam_AnalyticsAttr(FName(TEXT("RewardAmount")), FSeam_NetValue::MakeInt(Def.RewardAmount)));
	}

	ISeam_AnalyticsSink::Execute_RecordAggregateEvent(Provider, ProgTags::Bus_AchievementUnlocked, Attrs);
}

void UProg_AchievementSubsystem::ReportPlatformUnlock(const UProg_AchievementDefinition& Def) const
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* Provider = Locator->ResolveService(ProgTags::Service_PlatformTrophies);
	if (!Provider || !Provider->Implements<USeam_PlatformAchievements>())
	{
		return; // inert default: no platform bridge bound
	}

	ISeam_PlatformAchievements::Execute_UnlockPlatformAchievement(Provider, Def.DataTag);
}

void UProg_AchievementSubsystem::ReportPlatformProgress(const UProg_AchievementDefinition& Def, float Progress) const
{
	const UProg_DeveloperSettings* Settings = UProg_DeveloperSettings::Get();
	if (!Settings || !Settings->bReportPlatformProgress)
	{
		return; // progressive trophies disabled
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* Provider = Locator->ResolveService(ProgTags::Service_PlatformTrophies);
	if (!Provider || !Provider->Implements<USeam_PlatformAchievements>())
	{
		return;
	}

	ISeam_PlatformAchievements::Execute_SetPlatformAchievementProgress(
		Provider, Def.DataTag, FMath::Clamp(Progress, 0.f, 1.f));
}

void UProg_AchievementSubsystem::GrantReward(const UProg_AchievementDefinition& Def) const
{
	if (!Def.HasReward())
	{
		return; // no reward configured
	}

	// The wallet is authority-only for mutation; grant by resolving the local player's wallet seam. On a
	// client this resolves the locally-controlled wallet whose AddCurrency is a no-op, so the host's
	// grant is the one that takes effect — matching the wallet's replication model.
	TScriptInterface<ISeam_Wallet> Wallet = ResolveLocalWallet();
	if (!Wallet)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Prog] Reward for '%s' skipped: no wallet resolved."),
			*Def.DataTag.ToString());
		return;
	}

	// ISeam_Wallet is read-only by contract; the concrete provider is the wallet component, which owns the
	// authoritative AddCurrency. Resolve the concrete object and route the grant through ISeam_PurchaseTarget
	// if present (the canonical authority-write seam), otherwise fall back to nothing (read-only seam can't
	// mutate). The progression wallet implements both ISeam_Wallet (read) and exposes AddCurrency; reward
	// granting therefore goes through the purchase-target seam which the wallet's owner provides.
	UObject* WalletObject = Wallet.GetObject();
	if (WalletObject && WalletObject->Implements<USeam_PurchaseTarget>())
	{
		// PurchaseTarget treats the currency tag as the grantable item id with an int32 count; clamp the
		// (int64) reward to int32 range for the seam's signature, which is ample for currency rewards.
		const int32 Count = static_cast<int32>(
			FMath::Clamp<int64>(Def.RewardAmount, 0, static_cast<int64>(TNumericLimits<int32>::Max())));
		if (Count > 0 && ISeam_PurchaseTarget::Execute_CanReceive(WalletObject, Def.RewardCurrency, Count))
		{
			ISeam_PurchaseTarget::Execute_GrantItem(WalletObject, Def.RewardCurrency, Count);
		}
		return;
	}

	UE_LOG(LogDP, Verbose,
		TEXT("[Prog] Reward for '%s' skipped: wallet owner exposes no authority-write (ISeam_PurchaseTarget) seam."),
		*Def.DataTag.ToString());
}

TScriptInterface<ISeam_Wallet> UProg_AchievementSubsystem::ResolveLocalWallet() const
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	// The wallet lives on a player-owned actor: try the local player's controller, then its pawn, then
	// its player state. Resolve via the component-by-interface helper so we never hard-include the wallet
	// component's concrete header (HARD RULE 9).
	APlayerController* PC = GI->GetFirstLocalPlayerController(World);
	if (!PC)
	{
		return nullptr;
	}

	auto TryActor = [](AActor* Actor) -> TScriptInterface<ISeam_Wallet>
	{
		if (!Actor)
		{
			return nullptr;
		}
		if (Actor->Implements<USeam_Wallet>())
		{
			return TScriptInterface<ISeam_Wallet>(Actor);
		}
		if (UActorComponent* Comp = Actor->FindComponentByInterface(USeam_Wallet::StaticClass()))
		{
			return TScriptInterface<ISeam_Wallet>(Comp);
		}
		return nullptr;
	};

	if (TScriptInterface<ISeam_Wallet> W = TryActor(PC))           { return W; }
	if (TScriptInterface<ISeam_Wallet> W = TryActor(PC->GetPawn())) { return W; }
	if (TScriptInterface<ISeam_Wallet> W = TryActor(PC->PlayerState)) { return W; }

	return nullptr;
}

void UProg_AchievementSubsystem::BroadcastEvent(const FGameplayTag& Channel, const FProg_AchievementEvent& Event) const
{
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Event), const_cast<UProg_AchievementSubsystem*>(this));
	}
}

//~ Reset --------------------------------------------------------------------------------------

void UProg_AchievementSubsystem::ResetAllProgress()
{
	Flags.Reset();
	Counters.Reset();
	ChannelHits.Reset();
	Unlocked.Reset();
	LastReportedProgress.Reset();
	UE_LOG(LogDP, Log, TEXT("[Prog] AchievementSubsystem: all progress reset."));
}

//~ ISeam_Persistable --------------------------------------------------------------------------

void UProg_AchievementSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	// Capture is read-only and safe on any peer; we persist only the unlocked set (see record doc).
	FProg_AchievementSaveRecord Record;
	Record.UnlockedAchievements = Unlocked.Array();
	Out = FInstancedStruct::Make(Record);
}

void UProg_AchievementSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY guard per the ISeam_Persistable contract: a client-side load is a no-op (clients rebuild
	// their unlocked set from observed events / their own prior local save).
	if (!HasHostAuthority())
	{
		return;
	}

	if (!In.IsValid() || In.GetScriptStruct() != FProg_AchievementSaveRecord::StaticStruct())
	{
		UE_LOG(LogDP, Warning, TEXT("[Prog] AchievementSubsystem RestoreState: record missing or wrong type; skipping."));
		return;
	}

	const FProg_AchievementSaveRecord& Record = In.Get<FProg_AchievementSaveRecord>();

	Unlocked.Reset();
	LastReportedProgress.Reset();
	for (const FGameplayTag& Tag : Record.UnlockedAchievements)
	{
		if (Tag.IsValid())
		{
			Unlocked.Add(Tag);
			LastReportedProgress.Add(Tag, 1.f);
		}
	}

	UE_LOG(LogDP, Log, TEXT("[Prog] AchievementSubsystem: restored %d unlocked achievement(s)."),
		Unlocked.Num());

	// Restored unlocks must NOT re-fire delegates/trophies/rewards (those happened the session they
	// unlocked). A subsequent evaluation pass naturally skips them because they're already in Unlocked.
}

FGameplayTag UProg_AchievementSubsystem::GetPersistenceKind_Implementation() const
{
	return ProgTags::Persist_Achievements;
}

//~ Debug --------------------------------------------------------------------------------------

FString UProg_AchievementSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(
		TEXT("Achievements: defs=%d unlocked=%d flags=%d counters=%d channels=%d authority=%s"),
		Definitions.Num(), Unlocked.Num(), Flags.Num(), Counters.Num(), ChannelHits.Num(),
		HasHostAuthority() ? TEXT("yes") : TEXT("no"));
}

#undef LOCTEXT_NAMESPACE
