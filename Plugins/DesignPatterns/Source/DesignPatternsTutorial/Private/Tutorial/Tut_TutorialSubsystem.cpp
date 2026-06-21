// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tutorial/Tut_TutorialSubsystem.h"
#include "Tutorial/Tut_TutorialDefinition.h"
#include "Tutorial/Tut_TutorialViewModel.h"
#include "Settings/Tut_DeveloperSettings.h"
#include "DesignPatternsTutorialModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "UI/Seam_UIHighlight.h"
#include "Input/Seam_InputModeArbiter.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Net/Seam_NetValue.h"

// World-hub READ seam (resolved from the locator; never the concrete hub subsystem).
#include "Query/WorldHub_Queryable.h"
#include "Hub/WorldHub_Scope.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The data registry + service locator + message bus are sibling GI subsystems; declare the dependency so
	// they exist before us.
	Collection.InitializeDependency<UDP_ServiceLocatorSubsystem>();
	Collection.InitializeDependency<UDP_DataRegistrySubsystem>();
	Collection.InitializeDependency<UDP_MessageBusSubsystem>();

	ApplySettings();

	// Instanced ViewModel subobject — created with this subsystem as Outer so it shares our GC lifetime.
	ViewModel = NewObject<UTut_TutorialViewModel>(this, TEXT("TutorialViewModel"));

	RegisterBusListeners();

	// Auto-start configured tutorials (in order) that are not already completed.
	if (const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get())
	{
		for (const FGameplayTag& Tag : Settings->AutoStartTutorials)
		{
			if (Tag.IsValid() && !IsTutorialCompleted(Tag))
			{
				if (StartTutorial(Tag))
				{
					// Only one tutorial runs at a time; the rest auto-start once this one finishes via a
					// project's own sequencing. Stop after the first eligible auto-start.
					break;
				}
			}
		}
	}

	UE_LOG(LogDP, Log, TEXT("Tut_TutorialSubsystem initialized."));
}

void UTut_TutorialSubsystem::Deinitialize()
{
	// Tear down any active run (releases input lock + highlight) without recording a spurious completion.
	if (ActiveDefinition)
	{
		ClearStepHighlight();
		ReleaseStepInputGate();
		ActiveDefinition = nullptr;
		ActiveTutorialTag = FGameplayTag();
		ActiveStepIndex = INDEX_NONE;
	}

	if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	Super::Deinitialize();
}

void UTut_TutorialSubsystem::ApplySettings()
{
	if (const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLogging;
	}
}

// -------------------------------------------------------------------------------------------------
// Bus
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::RegisterBusListeners()
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Warning, TEXT("Tut_TutorialSubsystem: no message bus; bus-event conditions will never fire."));
		return;
	}

	// Listen broadly under the core DP.Bus root: every gameplay event a UTut_Condition_BusEvent might key on
	// is a child of it. We record the exact channel and re-evaluate the active step. This single subscription
	// keeps conditions decoupled from which specific channels exist.
	const FGameplayTag BusRoot =
		FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Bus")), /*ErrorIfNotFound=*/false);

	Bus->ListenNative(BusRoot,
		[this](const FDP_Message& Message)
		{
			HandleBusMessage(Message);
		},
		this, EDP_MessageMatch::ExactOrChild);
}

void UTut_TutorialSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	if (!Message.Channel.IsValid())
	{
		return;
	}

	// Record the channel as a seen one-shot event (until the next step entry clears the set).
	SeenEventTags.AddTag(Message.Channel);

	// A new event may satisfy the active step's trigger (surfacing it) or completion (advancing it).
	if (ActiveDefinition)
	{
		EvaluateActiveStep();
	}
}

// -------------------------------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------------------------------

bool UTut_TutorialSubsystem::StartTutorial(FGameplayTag TutorialTag)
{
	if (!TutorialTag.IsValid())
	{
		return false;
	}

	if (IsTutorialCompleted(TutorialTag))
	{
		UE_LOG(LogDP, Verbose, TEXT("Tut: '%s' already completed; not starting."), *TutorialTag.ToString());
		return false;
	}

	if (ActiveDefinition)
	{
		UE_LOG(LogDP, Verbose, TEXT("Tut: a tutorial ('%s') is already running; not starting '%s'."),
			*ActiveTutorialTag.ToString(), *TutorialTag.ToString());
		return false;
	}

	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	if (!Registry)
	{
		UE_LOG(LogDP, Warning, TEXT("Tut: no data registry; cannot resolve tutorial '%s'."), *TutorialTag.ToString());
		return false;
	}

	UTut_TutorialDefinition* Def = Registry->Find<UTut_TutorialDefinition>(TutorialTag);
	if (!Def)
	{
		UE_LOG(LogDP, Warning, TEXT("Tut: no UTut_TutorialDefinition registered under '%s'."), *TutorialTag.ToString());
		return false;
	}

	ActiveDefinition = Def;
	ActiveTutorialTag = TutorialTag;
	ActiveStepIndex = INDEX_NONE;

	BroadcastTutorialEvent(TutTags::Bus_TutorialStarted, ETut_TutorialStatus::Running);
	UE_LOG(LogDP, Log, TEXT("Tut: started '%s' (%d steps)."), *TutorialTag.ToString(), Def->Steps.Num());

	// An empty tutorial completes immediately.
	if (Def->Steps.Num() == 0)
	{
		FinishTutorial(/*bSkipped=*/false);
		return true;
	}

	EnterStep(0);
	return true;
}

bool UTut_TutorialSubsystem::SkipTutorial()
{
	if (!ActiveDefinition)
	{
		return false;
	}

	UE_LOG(LogDP, Log, TEXT("Tut: skipping '%s' at step %d."), *ActiveTutorialTag.ToString(), ActiveStepIndex);
	FinishTutorial(/*bSkipped=*/true);
	return true;
}

bool UTut_TutorialSubsystem::IsTutorialCompleted(FGameplayTag TutorialTag) const
{
	return TutorialTag.IsValid() && CompletedTutorials.HasTagExact(TutorialTag);
}

// -------------------------------------------------------------------------------------------------
// Step lifecycle
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::EnterStep(int32 Index)
{
	if (!ActiveDefinition || !ActiveDefinition->Steps.IsValidIndex(Index))
	{
		return;
	}

	// Re-arm: clear seen events so a step's bus-event triggers/completions only count events from now on.
	SeenEventTags.Reset();

	ActiveStepIndex = Index;
	bActiveStepSurfaced = false;

	const FTut_TutorialStep& Step = ActiveDefinition->Steps[Index];

	// If the step has no trigger, it surfaces immediately on entry; otherwise it waits for the trigger.
	if (Step.Trigger == nullptr)
	{
		EvaluateActiveStep();
	}
	else
	{
		// Push a "waiting for trigger" state so the UI can reflect the gap without an instruction yet.
		if (ViewModel)
		{
			ViewModel->ClearActive();
		}
		// Still allow an immediate evaluation in case the trigger is already satisfied (e.g. a hub flag).
		EvaluateActiveStep();
	}
}

void UTut_TutorialSubsystem::EvaluateActiveStep()
{
	if (!ActiveDefinition || !ActiveDefinition->Steps.IsValidIndex(ActiveStepIndex))
	{
		return;
	}

	const FTut_TutorialStep& Step = ActiveDefinition->Steps[ActiveStepIndex];

	// 1) Surface the step once its trigger is met (or immediately if it has none).
	if (!bActiveStepSurfaced)
	{
		const bool bTriggerMet = (Step.Trigger == nullptr)
			? true
			: Step.Trigger->Evaluate(this);

		if (!bTriggerMet)
		{
			return; // still waiting for the trigger
		}

		bActiveStepSurfaced = true;

		// Push the instruction to the ViewModel + native hook + bus.
		if (ViewModel)
		{
			ViewModel->SetActiveStep(ActiveTutorialTag, ActiveStepIndex, ActiveDefinition->Steps.Num(),
				Step.Instruction, Step.HighlightTargetTag);
		}
		ApplyStepHighlight(Step);
		ApplyStepInputGate(Step);

		OnTutorialStepChanged.Broadcast(ActiveTutorialTag, ActiveStepIndex);
		BroadcastTutorialEvent(TutTags::Bus_TutorialStepChanged, ETut_TutorialStatus::Running);

		UE_LOG(LogDP, Verbose, TEXT("Tut: surfaced step %d of '%s'."), ActiveStepIndex, *ActiveTutorialTag.ToString());
	}

	// 2) Advance once the completion condition is met.
	//
	// A step with NO completion condition is terminal-until-driven: if it also shows no instruction it is a
	// pure transitional step and auto-advances immediately; if it DOES show an instruction it stays on screen
	// until the project advances it (a project broadcasts a "next" bus event that completes a following step,
	// or calls SkipTutorial). This prevents an instruction-bearing step from flashing past in one frame.
	if (bActiveStepSurfaced)
	{
		bool bComplete = false;
		if (Step.Completion != nullptr)
		{
			bComplete = Step.Completion->Evaluate(this);
		}
		else
		{
			bComplete = Step.Instruction.IsEmpty();
		}

		if (bComplete)
		{
			AdvanceStep();
		}
	}
}

void UTut_TutorialSubsystem::AdvanceStep()
{
	if (!ActiveDefinition)
	{
		return;
	}

	// Tear down the finishing step's highlight + input gate before moving on.
	ClearStepHighlight();
	ReleaseStepInputGate();

	const int32 NextIndex = ActiveStepIndex + 1;
	if (ActiveDefinition->Steps.IsValidIndex(NextIndex))
	{
		EnterStep(NextIndex);
	}
	else
	{
		FinishTutorial(/*bSkipped=*/false);
	}
}

void UTut_TutorialSubsystem::FinishTutorial(bool bSkipped)
{
	const FGameplayTag FinishedTag = ActiveTutorialTag;
	const int32 ReachedStep = ActiveStepIndex;

	// Clear presentation.
	ClearStepHighlight();
	ReleaseStepInputGate();
	if (ViewModel)
	{
		ViewModel->ClearActive();
	}

	// Record completion so it never replays. A skip counts as completed only when settings say so.
	const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get();
	const bool bRecord = !bSkipped || (Settings ? Settings->bSkipCountsAsCompleted : true);
	if (bRecord && FinishedTag.IsValid())
	{
		CompletedTutorials.AddTag(FinishedTag);
	}

	// Reset active state.
	ActiveDefinition = nullptr;
	ActiveTutorialTag = FGameplayTag();
	ActiveStepIndex = INDEX_NONE;
	bActiveStepSurfaced = false;
	SeenEventTags.Reset();

	OnTutorialStepChanged.Broadcast(FinishedTag, INDEX_NONE);

	// Build + broadcast the completion event.
	{
		FTut_TutorialEvent Event;
		Event.TutorialTag = FinishedTag;
		Event.Status = ETut_TutorialStatus::Completed;
		Event.StepIndex = INDEX_NONE;
		if (UDP_MessageBusSubsystem* Bus =
				FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			Bus->BroadcastPayload(TutTags::Bus_TutorialCompleted,
				FInstancedStruct::Make(Event), this);
		}
	}

	// Mirror to analytics (opt-in, default-off seam).
	RecordAnalytics(bSkipped ? TutTags::Analytics_TutorialSkipped : TutTags::Analytics_TutorialCompleted, ReachedStep);

	UE_LOG(LogDP, Log, TEXT("Tut: %s '%s' (reached step %d)."),
		bSkipped ? TEXT("skipped") : TEXT("completed"), *FinishedTag.ToString(), ReachedStep);
}

// -------------------------------------------------------------------------------------------------
// Highlight / input gating (seam-driven)
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::ApplyStepHighlight(const FTut_TutorialStep& Step)
{
	if (!Step.HighlightTargetTag.IsValid())
	{
		return;
	}

	UObject* Highlighter = ResolveUIHighlight();
	if (!Highlighter)
	{
		// No UI highlight provider registered: degrade to no-op (the step still surfaces its instruction).
		UE_LOG(LogDP, Verbose, TEXT("Tut: no ISeam_UIHighlight provider; step highlight skipped."));
		return;
	}

	ISeam_UIHighlight::Execute_HighlightTarget(Highlighter, Step.HighlightTargetTag, Step.HighlightStyleTag);
	ActiveHighlightTarget = Step.HighlightTargetTag;
}

void UTut_TutorialSubsystem::ClearStepHighlight()
{
	if (!ActiveHighlightTarget.IsValid())
	{
		return;
	}

	if (UObject* Highlighter = ResolveUIHighlight())
	{
		ISeam_UIHighlight::Execute_ClearHighlight(Highlighter, ActiveHighlightTarget);
	}
	ActiveHighlightTarget = FGameplayTag();
}

void UTut_TutorialSubsystem::ApplyStepInputGate(const FTut_TutorialStep& Step)
{
	if (!Step.bGateInput || bHoldingInputMode)
	{
		return;
	}

	if (ActiveDefinition && ActiveDefinition->bNeverGateInput)
	{
		return; // master override on the definition disables all input gating.
	}

	UObject* Arbiter = ResolveInputArbiter();
	if (!Arbiter)
	{
		// No arbiter (e.g. Platform module absent): degrade gracefully — the tutorial still runs.
		UE_LOG(LogDP, Verbose, TEXT("Tut: no ISeam_InputModeArbiter; step runs without input gating."));
		return;
	}

	const UTut_DeveloperSettings* Settings = UTut_DeveloperSettings::Get();
	FGameplayTag ModeTag = Step.InputModeTag;
	if (!ModeTag.IsValid())
	{
		ModeTag = (Settings && Settings->DefaultTutorialInputModeTag.IsValid())
			? Settings->DefaultTutorialInputModeTag
			: FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Input.Mode.Tutorial")), /*ErrorIfNotFound=*/false);
	}
	const int32 Priority = Settings
		? Settings->TutorialInputModePriority
		: UTut_DeveloperSettings::FallbackTutorialInputModePriority;

	InputModeRequestId = ISeam_InputModeArbiter::Execute_PushInputMode(Arbiter, ModeTag, Priority);
	bHoldingInputMode = InputModeRequestId.IsValid();
}

void UTut_TutorialSubsystem::ReleaseStepInputGate()
{
	if (!bHoldingInputMode)
	{
		return;
	}

	if (UObject* Arbiter = ResolveInputArbiter())
	{
		ISeam_InputModeArbiter::Execute_PopInputMode(Arbiter, InputModeRequestId);
	}
	InputModeRequestId.Invalidate();
	bHoldingInputMode = false;
}

// -------------------------------------------------------------------------------------------------
// Seam resolution
// -------------------------------------------------------------------------------------------------

UObject* UTut_TutorialSubsystem::ResolveUIHighlight() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(TutTags::Service_UIHighlight);
	return (Obj && Obj->Implements<USeam_UIHighlight>()) ? Obj : nullptr;
}

UObject* UTut_TutorialSubsystem::ResolveInputArbiter() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(TutTags::Service_InputModeArbiter);
	return (Obj && Obj->Implements<USeam_InputModeArbiter>()) ? Obj : nullptr;
}

UObject* UTut_TutorialSubsystem::ResolveWorldHub() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(TutTags::Service_WorldHubQueryable);
	// IWorldHub_Queryable is a native C++ interface (not BlueprintNativeEvent); validate by Cast.
	return (Obj && Cast<IWorldHub_Queryable>(Obj)) ? Obj : nullptr;
}

// -------------------------------------------------------------------------------------------------
// ITut_ConditionContext
// -------------------------------------------------------------------------------------------------

bool UTut_TutorialSubsystem::HasSeenBusEvent(const FGameplayTag& EventTag) const
{
	if (!EventTag.IsValid())
	{
		return false;
	}
	// Exact-or-child match: a seen child channel satisfies a parent-tag condition.
	return SeenEventTags.HasTag(EventTag);
}

bool UTut_TutorialSubsystem::QueryHubValue(const FGameplayTag& Key, FInstancedStruct& Out) const
{
	if (!Key.IsValid())
	{
		return false;
	}

	UObject* HubObj = ResolveWorldHub();
	if (!HubObj)
	{
		return false; // no hub registered: conditions degrade to their documented absent-value behaviour.
	}

	const IWorldHub_Queryable* Hub = Cast<IWorldHub_Queryable>(HubObj);
	if (!Hub)
	{
		return false;
	}

	return Hub->QueryValue(Key, FWorldHub_Scope::Global(), Out);
}

// -------------------------------------------------------------------------------------------------
// ISeam_Persistable
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FTut_TutorialSaveRecord Record;
	Record.CompletedTutorials = CompletedTutorials;
	Out = FInstancedStruct::Make(Record);
}

void UTut_TutorialSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// This is LOCAL, profile-level state (which tutorials this player has seen). It is not authoritative
	// gameplay, so no HasAuthority guard is required — the contract's authority guard applies to participants
	// that restore replicated/world state, which this is not.
	if (In.IsValid() && In.GetScriptStruct() == FTut_TutorialSaveRecord::StaticStruct())
	{
		const FTut_TutorialSaveRecord& Record = In.Get<FTut_TutorialSaveRecord>();
		CompletedTutorials = Record.CompletedTutorials;
		UE_LOG(LogDP, Log, TEXT("Tut: restored %d completed tutorials from save."),
			CompletedTutorials.Num());
	}
}

FGameplayTag UTut_TutorialSubsystem::GetPersistenceKind_Implementation() const
{
	return TutTags::Persist_Kind_Tutorial;
}

// -------------------------------------------------------------------------------------------------
// Events / analytics / debug
// -------------------------------------------------------------------------------------------------

void UTut_TutorialSubsystem::BroadcastTutorialEvent(FGameplayTag Channel, ETut_TutorialStatus Status) const
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FTut_TutorialEvent Event;
	Event.TutorialTag = ActiveTutorialTag;
	Event.Status = Status;
	Event.StepIndex = ActiveStepIndex;
	Event.StepCount = ActiveDefinition ? ActiveDefinition->Steps.Num() : 0;
	if (ActiveDefinition && ActiveDefinition->Steps.IsValidIndex(ActiveStepIndex))
	{
		Event.Instruction = ActiveDefinition->Steps[ActiveStepIndex].Instruction;
	}

	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Event), const_cast<UTut_TutorialSubsystem*>(this));
}

void UTut_TutorialSubsystem::RecordAnalytics(FGameplayTag EventTag, int32 StepReached) const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* SinkObj = Locator->ResolveService(TutTags::Service_AnalyticsSink);
	if (!SinkObj || !SinkObj->Implements<USeam_AnalyticsSink>())
	{
		return; // analytics is optional and default-off; nothing to do.
	}

	if (!ISeam_AnalyticsSink::Execute_IsSinkReady(SinkObj))
	{
		return;
	}

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Emplace(FName(TEXT("Tutorial")), FSeam_NetValue::MakeTag(ActiveTutorialTag));
	Attrs.Emplace(FName(TEXT("StepReached")), FSeam_NetValue::MakeInt(StepReached));
	ISeam_AnalyticsSink::Execute_RecordAggregateEvent(SinkObj, EventTag, Attrs);
}

FString UTut_TutorialSubsystem::GetDPDebugString_Implementation() const
{
	if (!ActiveDefinition)
	{
		return FString::Printf(TEXT("Tutorial: idle (%d completed)"), CompletedTutorials.Num());
	}
	return FString::Printf(TEXT("Tutorial '%s' step %d/%d (%s)"),
		*ActiveTutorialTag.ToString(),
		ActiveStepIndex + 1,
		ActiveDefinition->Steps.Num(),
		bActiveStepSurfaced ? TEXT("surfaced") : TEXT("waiting trigger"));
}
