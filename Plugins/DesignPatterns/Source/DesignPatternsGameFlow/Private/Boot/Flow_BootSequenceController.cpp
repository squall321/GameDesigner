// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Boot/Flow_BootSequenceController.h"
#include "Boot/Flow_BootSequenceDefinition.h"
#include "Boot/Flow_BootStepDefinition.h"
#include "Flow/Flow_GameFlowSubsystem.h"
#include "Flow/Flow_FlowTypes.h"
#include "Flow/Flow_OrchestratorTypes.h"
#include "Loading/Flow_LoadingScreenSubsystem.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Save/DPSaveGameSubsystem.h"

#include "Persist/Seam_SaveSlotManager.h"

#include "Engine/World.h"
#include "TimerManager.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_BootSequenceController::Initialize(UFlow_GameFlowSubsystem* InOwner)
{
	Owner = InOwner;
}

void UFlow_BootSequenceController::Shutdown()
{
	if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(StepTimer);
	}
	// Cancel any in-flight preload so we don't leave the loading screen up.
	if (UFlow_LoadingScreenSubsystem* Loading = FDP_SubsystemStatics::GetGameInstanceSubsystem<UFlow_LoadingScreenSubsystem>(this))
	{
		Loading->CancelPreload();
	}
	Sequence = nullptr;
	bRunning = false;
	Owner.Reset();
}

// ---------------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------------

void UFlow_BootSequenceController::StartBoot()
{
	if (bRunning || bComplete)
	{
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();

#if WITH_EDITOR
	// Skip the boot sequence in PIE when configured, so iteration jumps straight to the initial phase.
	const UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	if (Settings && Settings->bSkipBootInPIE && World && World->WorldType == EWorldType::PIE)
	{
		UE_LOG(LogDP, Log, TEXT("[Flow][Boot] Skipping boot sequence in PIE."));
		CompleteBoot();
		return;
	}
#endif

	Sequence = ResolveSequence();
	if (!Sequence || Sequence->Steps.Num() == 0)
	{
		UE_LOG(LogDP, Log, TEXT("[Flow][Boot] No boot sequence authored; completing boot immediately."));
		CompleteBoot();
		return;
	}

	bRunning = true;
	bComplete = false;
	CurrentStepIndex = INDEX_NONE;

	// Pick a sensible step cadence (small fixed boot poll; not a magic gameplay number — it is a UI poll rate).
	StepTickInterval = 0.1f;

	AdvanceStep();
}

UFlow_BootSequenceDefinition* UFlow_BootSequenceController::ResolveSequence() const
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	if (!Settings || Settings->BootSequence.IsNull())
	{
		return nullptr;
	}
	// LoadSynchronous is fine: a boot sequence is a tiny metadata asset resolved once at startup.
	return Settings->BootSequence.LoadSynchronous();
}

bool UFlow_BootSequenceController::IsFirstRun() const
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	return !Settings || !Settings->bHasCompletedFirstRun;
}

// ---------------------------------------------------------------------------------------------------
// Step machine
// ---------------------------------------------------------------------------------------------------

void UFlow_BootSequenceController::AdvanceStep()
{
	if (!Sequence)
	{
		CompleteBoot();
		return;
	}

	const bool bFirstRun = IsFirstRun();

	// Find the next applicable step, skipping first-run-only steps when this is not the first run.
	int32 Next = CurrentStepIndex + 1;
	while (Sequence->Steps.IsValidIndex(Next))
	{
		const UFlow_BootStepDefinition* Step = Sequence->Steps[Next];
		if (Step && (!Step->bFirstRunOnly || bFirstRun))
		{
			break;
		}
		++Next;
	}

	if (!Sequence->Steps.IsValidIndex(Next))
	{
		CompleteBoot();
		return;
	}

	CurrentStepIndex = Next;
	EnterStep();
}

void UFlow_BootSequenceController::EnterStep()
{
	const UFlow_BootStepDefinition* Step = Sequence->Steps[CurrentStepIndex];
	check(Step);

	StepElapsed = 0.f;
	bProfileLoadDone = false;
	bProfileLoadInFlight = false;

	UE_LOG(LogDP, Log, TEXT("[Flow][Boot] Step %d/%d kind=%s."),
		CurrentStepIndex + 1, Sequence->Steps.Num(),
		Step->StepKind.IsValid() ? *Step->StepKind.ToString() : TEXT("<none>"));

	// Push the step's screen (if any) via the message bus, exactly like the flow subsystem's screen push.
	if (Step->ScreenTag.IsValid())
	{
		if (UDP_MessageBusSubsystem* Bus = GetBus())
		{
			const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
			FFlow_ScreenRequestPayload Payload;
			Payload.ScreenTag = Step->ScreenTag;
			Payload.LayerTag = Step->LayerTag.IsValid()
				? Step->LayerTag
				: (Settings ? Settings->DefaultScreenLayerTag : FGameplayTag());
			Payload.OwningPhase = FlowTags::Phase_Boot;
			Bus->BroadcastPayload(FlowTags::Bus_ScreenPush, FInstancedStruct::Make(Payload), this);
		}
	}

	// Kick the step's work.
	UFlow_LoadingScreenSubsystem* Loading = FDP_SubsystemStatics::GetGameInstanceSubsystem<UFlow_LoadingScreenSubsystem>(this);

	if (Step->StepKind == FlowTags::BootStep_ProfileLoad)
	{
		// Load the most-recent profile slot through the save-slot seam + core save subsystem.
		const UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
		UObject* SlotMgr = Locator ? Locator->ResolveService(FlowTags::Service_SaveSlotManager) : nullptr;
		const FString Slot = (SlotMgr && SlotMgr->Implements<USeam_SaveSlotManager>())
			? ISeam_SaveSlotManager::Execute_GetMostRecentSlot(SlotMgr)
			: FString();

		if (!Slot.IsEmpty())
		{
			if (UDP_SaveGameSubsystem* Save = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(this))
			{
				bProfileLoadInFlight = true;
					FDP_LoadCallbackDynamic OnLoaded;
					OnLoaded.BindUFunction(this, FName(TEXT("HandleProfileLoaded")));
					Save->LoadAsync(Slot, OnLoaded);
			}
			else
			{
				bProfileLoadDone = true; // No save subsystem: treat as done so boot never wedges.
			}
		}
		else
		{
			bProfileLoadDone = true; // No profile slot: nothing to load.
		}
	}
	else if (Step->Preload.Num() > 0 && Loading)
	{
		// Front-load this step's assets through the existing loading screen (real fraction).
		Loading->BeginPreload(Step->Preload, Step->StatusLabel, /*TargetMapName*/ FString());
	}

	BroadcastStep(/*bCompleteNow*/ false);

	// Begin the step timer.
	if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
	{
		World->GetTimerManager().SetTimer(StepTimer, FTimerDelegate::CreateUObject(this, &UFlow_BootSequenceController::TickStep),
			StepTickInterval, /*bLoop*/ true);
	}
	else
	{
		// No world/timer: can't gate on time, so advance once work is (or becomes) complete next frame.
		if (IsActiveStepWorkComplete())
		{
			AdvanceStep();
		}
	}
}

void UFlow_BootSequenceController::TickStep()
{
	if (!Sequence || !Sequence->Steps.IsValidIndex(CurrentStepIndex))
	{
		CompleteBoot();
		return;
	}

	const UFlow_BootStepDefinition* Step = Sequence->Steps[CurrentStepIndex];
	StepElapsed += StepTickInterval;

	const bool bMinElapsed = StepElapsed >= Step->MinSeconds;
	const bool bTimedOut = (Step->TimeoutSeconds > 0.f) && (StepElapsed >= Step->TimeoutSeconds);
	const bool bWorkDone = !Step->bGatesOnComplete || IsActiveStepWorkComplete();

	if (bTimedOut)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][Boot] Step %d timed out after %.1fs; force-advancing."),
			CurrentStepIndex, StepElapsed);
	}

	if ((bMinElapsed && bWorkDone) || bTimedOut)
	{
		// Stop the step timer before advancing (EnterStep re-arms it for the next step).
		if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
		{
			World->GetTimerManager().ClearTimer(StepTimer);
		}
		AdvanceStep();
	}
}

bool UFlow_BootSequenceController::IsActiveStepWorkComplete() const
{
	if (!Sequence || !Sequence->Steps.IsValidIndex(CurrentStepIndex))
	{
		return true;
	}
	const UFlow_BootStepDefinition* Step = Sequence->Steps[CurrentStepIndex];

	if (Step->StepKind == FlowTags::BootStep_ProfileLoad)
	{
		return bProfileLoadDone && !bProfileLoadInFlight;
	}

	if (Step->Preload.Num() > 0)
	{
		// The loading subsystem returns to Idle once the preload completes (and the engine map load, if any).
		if (const UFlow_LoadingScreenSubsystem* Loading =
				FDP_SubsystemStatics::GetGameInstanceSubsystem<UFlow_LoadingScreenSubsystem>(this))
		{
			return Loading->GetLoadingState() == EFlow_LoadingState::Idle || Loading->GetProgress() >= 1.f;
		}
	}

	// A pure timed step (legal/unknown) has no async work; it is "done" the moment MinSeconds elapses.
	return true;
}

void UFlow_BootSequenceController::CompleteBoot()
{
	bComplete = true;
	bRunning = false;
	CurrentStepIndex = INDEX_NONE;

	if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(StepTimer);
	}

	// Clear the first-run flag so first-run-only steps are skipped on subsequent launches.
	if (IsFirstRun())
	{
		if (UFlow_DeveloperSettings* MutableSettings = GetMutableDefault<UFlow_DeveloperSettings>())
		{
			MutableSettings->bHasCompletedFirstRun = true;
			MutableSettings->SaveConfig();
		}
	}

	BroadcastStep(/*bCompleteNow*/ true);

	// Transition the FSM to the configured initial phase (the validated request; Boot's allowed edges).
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const FGameplayTag InitialPhase = (Settings && Settings->InitialPhase.IsValid())
		? Settings->InitialPhase
		: FlowTags::Phase_MainMenu;

	if (UFlow_GameFlowSubsystem* Flow = Owner.Get())
	{
		// The initial phase is the post-boot destination, NOT Boot itself; if a project left InitialPhase at
		// Boot, advance to MainMenu so boot can't loop.
		const FGameplayTag Dest = (InitialPhase == FlowTags::Phase_Boot) ? FlowTags::Phase_MainMenu : InitialPhase;
		Flow->RequestTransition_Implementation(Dest);
	}

	UE_LOG(LogDP, Log, TEXT("[Flow][Boot] Boot sequence complete."));
}

void UFlow_BootSequenceController::BroadcastStep(bool bCompleteNow)
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}

	FFlow_BootStepPayload Payload;
	Payload.StepCount = Sequence ? Sequence->Steps.Num() : 0;
	Payload.bComplete = bCompleteNow;
	if (!bCompleteNow && Sequence && Sequence->Steps.IsValidIndex(CurrentStepIndex))
	{
		Payload.StepIndex = CurrentStepIndex;
		Payload.StepKind = Sequence->Steps[CurrentStepIndex]->StepKind;
	}
	Bus->BroadcastPayload(FlowTags::Bus_BootStepChanged, FInstancedStruct::Make(Payload), this);
}

UDP_MessageBusSubsystem* UFlow_BootSequenceController::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

void UFlow_BootSequenceController::HandleProfileLoaded(const FString& Slot, EDP_SaveResult Result, UDP_SaveGame* /*SaveObject*/)
{
	// Profile-load step finished (success or failure). Either way the step is no longer gating — a failed
	// profile load must not wedge boot; the game can surface a "new profile" path downstream.
	bProfileLoadInFlight = false;
	bProfileLoadDone = true;

	if (Result != EDP_SaveResult::Success)
	{
		UE_LOG(LogDP, Warning, TEXT("[Flow][Boot] Profile load of slot '%s' failed (result %d); continuing boot."),
			*Slot, static_cast<int32>(Result));
	}
}
