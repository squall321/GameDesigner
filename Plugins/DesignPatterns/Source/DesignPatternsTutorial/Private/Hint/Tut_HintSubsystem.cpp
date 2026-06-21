// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hint/Tut_HintSubsystem.h"
#include "Hint/Tut_HintDefinition.h"
#include "Tutorial/Tut_TutorialTypes.h"
#include "Settings/Tut_DeveloperSettings.h"
#include "DesignPatternsTutorialModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Query/WorldHub_Queryable.h"
#include "Hub/WorldHub_Scope.h"

#include "Engine/World.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

void UTut_HintSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UDP_ServiceLocatorSubsystem>();
	Collection.InitializeDependency<UDP_DataRegistrySubsystem>();
	Collection.InitializeDependency<UDP_MessageBusSubsystem>();

	ApplySettings();
	RegisterBusListeners();

	// Register configured hints.
	if (const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get())
	{
		for (const FGameplayTag& Tag : Settings->RegisteredHints)
		{
			RegisterHint(Tag);
		}
	}

	UE_LOG(LogDP, Log, TEXT("Tut_HintSubsystem initialized with %d hints."), RegisteredHints.Num());
}

void UTut_HintSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	RegisteredHints.Empty();
	HintRuntime.Empty();

	Super::Deinitialize();
}

void UTut_HintSubsystem::ApplySettings()
{
	const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get();

	if (Settings)
	{
		bEnableVerboseLogging = Settings->bVerboseLogging;
		EvaluationInterval = FMath::Max(Settings->HintEvaluationIntervalSeconds, 0.05f);
	}
	else
	{
		// Documented defensive fallback when the settings CDO is unavailable (very early load).
		EvaluationInterval = UTut_DeveloperSettings::DefaultHintEvaluationIntervalSeconds;
	}

	// (Re)install the periodic ticker at the configured cadence.
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTut_HintSubsystem::TickEvaluate),
		EvaluationInterval);
}

// -------------------------------------------------------------------------------------------------
// Registration
// -------------------------------------------------------------------------------------------------

bool UTut_HintSubsystem::RegisterHint(FGameplayTag HintTag)
{
	if (!HintTag.IsValid())
	{
		return false;
	}

	if (RegisteredHints.Contains(HintTag))
	{
		return true; // idempotent
	}

	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	if (!Registry)
	{
		return false;
	}

	UTut_HintDefinition* Def = Registry->Find<UTut_HintDefinition>(HintTag);
	if (!Def)
	{
		UE_LOG(LogDP, Warning, TEXT("Tut_Hint: no UTut_HintDefinition registered under '%s'."), *HintTag.ToString());
		return false;
	}

	RegisteredHints.Add(HintTag, Def);

	FHintRuntime Runtime;
	Runtime.Definition = Def;
	HintRuntime.Add(HintTag, Runtime);

	UE_LOG(LogDP, Verbose, TEXT("Tut_Hint: registered '%s' (priority %d)."), *HintTag.ToString(), Def->Priority);
	return true;
}

bool UTut_HintSubsystem::UnregisterHint(FGameplayTag HintTag)
{
	const bool bRemoved = RegisteredHints.Remove(HintTag) > 0;
	HintRuntime.Remove(HintTag);
	return bRemoved;
}

void UTut_HintSubsystem::ResetHintState()
{
	for (TPair<FGameplayTag, FHintRuntime>& Pair : HintRuntime)
	{
		Pair.Value.LastShownTime = -1.0;
		Pair.Value.ShowCount = 0;
	}
	LastAnyHintShownTime = -1.0;
}

// -------------------------------------------------------------------------------------------------
// Bus + ticking
// -------------------------------------------------------------------------------------------------

void UTut_HintSubsystem::RegisterBusListeners()
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	const FGameplayTag BusRoot =
		FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Bus")), /*ErrorIfNotFound=*/false);

	Bus->ListenNative(BusRoot,
		[this](const FDP_Message& Message)
		{
			HandleBusMessage(Message);
		},
		this, EDP_MessageMatch::ExactOrChild);
}

void UTut_HintSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	if (!Message.Channel.IsValid())
	{
		return;
	}

	SeenEventTags.AddTag(Message.Channel);

	// Evaluate only the hints whose TriggerEventTags match this channel (event-driven hints). State-driven
	// hints are caught by the periodic TickEvaluate pass.
	TArray<FGameplayTag> Candidates;
	for (const TPair<FGameplayTag, TObjectPtr<UTut_HintDefinition>>& Pair : RegisteredHints)
	{
		const UTut_HintDefinition* Def = Pair.Value;
		if (Def && Def->TriggerEventTags.HasTag(Message.Channel))
		{
			Candidates.Add(Pair.Key);
		}
	}

	if (Candidates.Num() > 0)
	{
		EvaluateAndSurface(Candidates);
	}
}

bool UTut_HintSubsystem::TickEvaluate(float /*DeltaTime*/)
{
	// Periodic pass over ALL hints (state-driven hints have no trigger channels).
	EvaluateAndSurface(TArray<FGameplayTag>());

	// Clear the short seen-event window so a one-shot bus event does not keep re-satisfying a hint forever.
	SeenEventTags.Reset();

	return true; // keep ticking
}

void UTut_HintSubsystem::EvaluateHintsNow()
{
	EvaluateAndSurface(TArray<FGameplayTag>());
}

// -------------------------------------------------------------------------------------------------
// Evaluation + surfacing
// -------------------------------------------------------------------------------------------------

void UTut_HintSubsystem::EvaluateAndSurface(const TArray<FGameplayTag>& Candidates)
{
	const double Now = GetNowSeconds();

	// Global inter-hint cooldown gate.
	const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get();
	const float GlobalCooldown = Settings
		? Settings->GlobalHintCooldownSeconds
		: UTut_DeveloperSettings::DefaultGlobalHintCooldownSeconds;
	if (LastAnyHintShownTime >= 0.0 && (Now - LastAnyHintShownTime) < GlobalCooldown)
	{
		return; // a hint surfaced too recently; suppress this pass.
	}

	// Pick the highest-priority eligible hint among the candidate set (or all when empty).
	FGameplayTag BestTag;
	int32 BestPriority = TNumericLimits<int32>::Min();

	auto Consider = [&](const FGameplayTag& HintTag)
	{
		FHintRuntime* Runtime = HintRuntime.Find(HintTag);
		if (!Runtime)
		{
			return;
		}
		if (!IsHintEligible(HintTag, *Runtime, Now))
		{
			return;
		}
		const UTut_HintDefinition* Def = Runtime->Definition.Get();
		const int32 Priority = Def ? Def->Priority : 0;
		if (Priority > BestPriority)
		{
			BestPriority = Priority;
			BestTag = HintTag;
		}
	};

	if (Candidates.Num() > 0)
	{
		for (const FGameplayTag& Tag : Candidates)
		{
			Consider(Tag);
		}
	}
	else
	{
		for (const TPair<FGameplayTag, FHintRuntime>& Pair : HintRuntime)
		{
			Consider(Pair.Key);
		}
	}

	if (BestTag.IsValid())
	{
		if (FHintRuntime* Runtime = HintRuntime.Find(BestTag))
		{
			SurfaceHint(BestTag, *Runtime);
		}
	}
}

bool UTut_HintSubsystem::IsHintEligible(FGameplayTag HintTag, const FHintRuntime& Runtime, double NowSeconds) const
{
	const UTut_HintDefinition* Def = Runtime.Definition.Get();
	if (!Def)
	{
		return false;
	}

	// Show-count cap.
	if (Def->MaxShowCount > 0 && Runtime.ShowCount >= Def->MaxShowCount)
	{
		return false;
	}

	// Per-hint cooldown.
	if (Runtime.LastShownTime >= 0.0 && (NowSeconds - Runtime.LastShownTime) < Def->CooldownSeconds)
	{
		return false;
	}

	// Condition (must be satisfied). A hint with no condition is never auto-eligible.
	if (Def->Condition == nullptr)
	{
		return false;
	}

	// const_cast: Evaluate takes a non-const WorldContext (this subsystem as ITut_ConditionContext); the
	// evaluation does not mutate this subsystem's logical state.
	return Def->Condition->Evaluate(const_cast<UTut_HintSubsystem*>(this));
}

void UTut_HintSubsystem::SurfaceHint(FGameplayTag HintTag, FHintRuntime& Runtime)
{
	const UTut_HintDefinition* Def = Runtime.Definition.Get();
	if (!Def)
	{
		return;
	}

	const double Now = GetNowSeconds();
	Runtime.LastShownTime = Now;
	Runtime.ShowCount += 1;
	LastAnyHintShownTime = Now;

	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get();
	const float DisplaySeconds = (Def->DisplaySeconds > 0.f)
		? Def->DisplaySeconds
		: (Settings ? Settings->DefaultHintDisplaySeconds : UTut_DeveloperSettings::FallbackHintDisplaySeconds);

	// 1) Surface as a HUD toast via the shared HUD notify channel (HUD adapts the request; no header coupling).
	{
		FTut_HintNotifyRequest Request;
		Request.Category = Def->NotificationCategory.IsValid() ? Def->NotificationCategory : TutTags::HUD_Notify_Hint;
		Request.Body = Def->Text;
		Request.Duration = DisplaySeconds;
		Request.Priority = Def->Priority;
		Request.DedupeKey = HintTag; // same hint replaces rather than stacks on the HUD side.

		Bus->BroadcastPayload(TutTags::Bus_HUD_Notify, FInstancedStruct::Make(Request), this);
	}

	// 2) Emit the telemetry/mirror event.
	{
		FTut_HintEvent Event;
		Event.HintTag = HintTag;
		Event.Text = Def->Text;
		Event.Priority = Def->Priority;

		Bus->BroadcastPayload(TutTags::Bus_HintShown, FInstancedStruct::Make(Event), this);
	}

	UE_LOG(LogDP, Verbose, TEXT("Tut_Hint: surfaced '%s' (priority %d, show %d)."),
		*HintTag.ToString(), Def->Priority, Runtime.ShowCount);
}

// -------------------------------------------------------------------------------------------------
// ITut_ConditionContext
// -------------------------------------------------------------------------------------------------

bool UTut_HintSubsystem::HasSeenBusEvent(const FGameplayTag& EventTag) const
{
	return EventTag.IsValid() && SeenEventTags.HasTag(EventTag);
}

bool UTut_HintSubsystem::QueryHubValue(const FGameplayTag& Key, FInstancedStruct& Out) const
{
	if (!Key.IsValid())
	{
		return false;
	}

	UObject* HubObj = ResolveWorldHub();
	const IWorldHub_Queryable* Hub = HubObj ? Cast<IWorldHub_Queryable>(HubObj) : nullptr;
	if (!Hub)
	{
		return false;
	}

	return Hub->QueryValue(Key, FWorldHub_Scope::Global(), Out);
}

UObject* UTut_HintSubsystem::ResolveWorldHub() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(TutTags::Service_WorldHubQueryable);
	return (Obj && Cast<IWorldHub_Queryable>(Obj)) ? Obj : nullptr;
}

double UTut_HintSubsystem::GetNowSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds();
	}
	// Defensive fallback when no world is available (e.g. during teardown): a monotonic platform clock.
	return FPlatformTime::Seconds();
}

FString UTut_HintSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Hints: %d registered, last shown %.1fs ago"),
		RegisteredHints.Num(),
		(LastAnyHintShownTime >= 0.0) ? (GetNowSeconds() - LastAnyHintShownTime) : -1.0);
}
