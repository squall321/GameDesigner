// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Flow_GameFlowSubsystem.h"
#include "Flow/Flow_FlowStateDefinition.h"
#include "Flow/Flow_FlowTypes.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "Input/Seam_InputModeArbiter.h"
#include "Persist/Seam_SaveSlotManager.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_GameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Register as the shared ISeam_AppFlowController provider so tutorial / AI-director / save UI can
	// read and drive the phase via the seam without depending on this module. We register WeakObserved:
	// the GameInstance already owns this subsystem, so the locator must only OBSERVE us (a strong ref
	// would be a redundant self-cycle). We unregister explicitly in Deinitialize before the GI tears us
	// down, so the weak entry never dangles.
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		bRegisteredAsService = Locator->RegisterService(
			FlowTags::Service_AppFlowController, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
		UE_LOG(LogDP, Log, TEXT("[GameFlow] Registered ISeam_AppFlowController service: %s"),
			bRegisteredAsService ? TEXT("ok") : TEXT("FAILED"));
	}

	// Enter the configured initial phase (defensive default: Flow.Phase.Boot). We force it because there
	// is no source phase to validate against yet.
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const FGameplayTag StartPhase = (Settings && Settings->InitialPhase.IsValid())
		? Settings->InitialPhase
		: FlowTags::Phase_Boot;

	DoTransition(StartPhase, /*bForce*/ true);
}

void UFlow_GameFlowSubsystem::Deinitialize()
{
	// Release any input-mode push we own so we don't leave the arbiter stack dirty across travel.
	PopInputMode();

	if (bRegisteredAsService)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
		{
			Locator->UnregisterService(FlowTags::Service_AppFlowController);
		}
		bRegisteredAsService = false;
	}

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------------
// ISeam_AppFlowController
// ---------------------------------------------------------------------------------------------------

FGameplayTag UFlow_GameFlowSubsystem::GetActivePhase_Implementation() const
{
	return ActivePhase;
}

bool UFlow_GameFlowSubsystem::RequestTransition_Implementation(FGameplayTag PhaseTag)
{
	if (!PhaseTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[GameFlow] RequestTransition with invalid phase tag rejected."));
		return false;
	}

	if (PhaseTag == ActivePhase)
	{
		// Idempotent: already in the requested phase. Treat as success without re-running side effects.
		return true;
	}

	if (!CanEnterPhase_Implementation(PhaseTag))
	{
		UE_LOG(LogDP, Verbose, TEXT("[GameFlow] Transition %s -> %s not allowed."),
			*ActivePhase.ToString(), *PhaseTag.ToString());
		return false;
	}

	return DoTransition(PhaseTag, /*bForce*/ false);
}

bool UFlow_GameFlowSubsystem::CanEnterPhase_Implementation(FGameplayTag PhaseTag) const
{
	if (!PhaseTag.IsValid())
	{
		return false;
	}

	// Same phase is always "enterable" (no-op transition).
	if (PhaseTag == ActivePhase)
	{
		return true;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const bool bUndeclaredAllowed = !Settings || Settings->bAllowUndeclaredTransitions;

	// If no source phase is set yet (pre-Boot), any transition is allowed.
	if (!ActivePhase.IsValid())
	{
		return true;
	}

	const UFlow_FlowStateDefinition* SourceDef = ResolvePhaseDefinition(ActivePhase);
	if (!SourceDef)
	{
		// No authored definition for the source phase: fall back to the project-wide policy.
		return bUndeclaredAllowed;
	}

	// Strict mode passes bUndeclaredAllowed=false so an explicit edge set is a hard whitelist.
	return SourceDef->AllowsTransitionTo(PhaseTag, bUndeclaredAllowed);
}

bool UFlow_GameFlowSubsystem::ForceTransition(FGameplayTag PhaseTag)
{
	if (!PhaseTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[GameFlow] ForceTransition with invalid phase tag rejected."));
		return false;
	}
	if (PhaseTag == ActivePhase)
	{
		return true;
	}
	return DoTransition(PhaseTag, /*bForce*/ true);
}

// ---------------------------------------------------------------------------------------------------
// Transition core
// ---------------------------------------------------------------------------------------------------

bool UFlow_GameFlowSubsystem::DoTransition(FGameplayTag NewPhase, bool bForce)
{
	const FGameplayTag OldPhase = ActivePhase;
	const UFlow_FlowStateDefinition* OldDef = OldPhase.IsValid() ? ResolvePhaseDefinition(OldPhase) : nullptr;
	const UFlow_FlowStateDefinition* NewDef = ResolvePhaseDefinition(NewPhase);

	UE_LOG(LogDP, Log, TEXT("[GameFlow] Transition %s -> %s%s"),
		OldPhase.IsValid() ? *OldPhase.ToString() : TEXT("<none>"),
		*NewPhase.ToString(),
		bForce ? TEXT(" (forced)") : TEXT(""));

	// 1) Leave the old phase (pop its screen / input mode / unpause).
	if (OldPhase.IsValid())
	{
		ApplyLeaveEffects(OldDef, OldPhase);
	}

	// 2) Commit the new active phase BEFORE enter effects so anything reacting to the bus / seam reads
	//    the new phase consistently.
	PreviousPhase = OldPhase;
	ActivePhase = NewPhase;

	// 3) Enter the new phase (travel / push screen / push input mode / pause).
	ApplyEnterEffects(NewDef, NewPhase);

	// 4) Announce the change on the bus so any module can react without depending on us.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		FFlow_PhaseChangedPayload Payload;
		Payload.PreviousPhase = OldPhase;
		Payload.NewPhase = NewPhase;
		Payload.bForced = bForce;
		Bus->BroadcastPayload(FlowTags::Bus_PhaseChanged, FInstancedStruct::Make(Payload), this);
	}

	return true;
}

void UFlow_GameFlowSubsystem::ApplyLeaveEffects(const UFlow_FlowStateDefinition* OldDef, FGameplayTag OldPhase)
{
	// Pop the old phase's screen and input mode. Unpause if the old phase paused the game.
	PopScreenForPhase(OldDef, OldPhase);
	PopInputMode();

	if (OldDef && OldDef->bPausesGame)
	{
		SetGamePaused(false);
	}
}

void UFlow_GameFlowSubsystem::ApplyEnterEffects(const UFlow_FlowStateDefinition* NewDef, FGameplayTag NewPhase)
{
	// Level travel first (so the screen/input apply to the destination world for non-traveling phases,
	// and a traveling phase's screen is pushed before the load on this machine for an immediate UI).
	TravelForPhase(NewDef);

	// Pause if requested (Pause phase).
	if (NewDef && NewDef->bPausesGame)
	{
		SetGamePaused(true);
	}

	// Push the phase screen and input mode.
	PushScreenForPhase(NewDef, NewPhase);
	PushInputModeForPhase(NewDef, NewPhase);
}

// ---------------------------------------------------------------------------------------------------
// Side-effect helpers
// ---------------------------------------------------------------------------------------------------

void UFlow_GameFlowSubsystem::TravelForPhase(const UFlow_FlowStateDefinition* Def)
{
	if (!Def || Def->Level.IsNull())
	{
		// Overlay phase (Pause/Results) or unauthored: stay in the current world.
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogDP, Warning, TEXT("[GameFlow] TravelForPhase: no world; skipping travel."));
		return;
	}

	// Build the travel URL: long package name + designer travel options.
	const FSoftObjectPath LevelPath = Def->Level.ToSoftObjectPath();
	FString MapName = LevelPath.GetLongPackageName();
	if (MapName.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("[GameFlow] TravelForPhase: empty map path; skipping travel."));
		return;
	}

	const FString URL = Def->TravelOptions.IsEmpty() ? MapName : (MapName + Def->TravelOptions);

	if (HasTravelAuthority())
	{
		// Host / standalone: a server-side open level resets the world (absolute) and brings clients along.
		UE_LOG(LogDP, Log, TEXT("[GameFlow] OpenLevel '%s' (authority)."), *URL);
		UGameplayStatics::OpenLevel(World, FName(*MapName), Def->bAbsoluteTravel, Def->TravelOptions);
	}
	else if (APlayerController* PC = GetLocalPlayerController())
	{
		// Pure client: request a client travel to the destination.
		const ETravelType TravelType = Def->bAbsoluteTravel ? TRAVEL_Absolute : TRAVEL_Relative;
		UE_LOG(LogDP, Log, TEXT("[GameFlow] ClientTravel '%s'."), *URL);
		PC->ClientTravel(URL, TravelType);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[GameFlow] TravelForPhase: no authority and no local PC; cannot travel to '%s'."), *URL);
	}
}

void UFlow_GameFlowSubsystem::PushScreenForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase)
{
	if (!Def || !Def->ScreenTag.IsValid())
	{
		return; // This phase pushes no screen.
	}

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[GameFlow] PushScreen: no message bus; screen request inert."));
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();

	FFlow_ScreenRequestPayload Payload;
	Payload.ScreenTag = Def->ScreenTag;
	Payload.LayerTag = Def->ScreenLayerTag.IsValid()
		? Def->ScreenLayerTag
		: (Settings ? Settings->DefaultScreenLayerTag : FGameplayTag());
	Payload.OwningPhase = Phase;

	Bus->BroadcastPayload(FlowTags::Bus_ScreenPush, FInstancedStruct::Make(Payload), this);
}

void UFlow_GameFlowSubsystem::PopScreenForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase)
{
	if (!Def || !Def->ScreenTag.IsValid())
	{
		return;
	}

	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();

	FFlow_ScreenRequestPayload Payload;
	Payload.ScreenTag = Def->ScreenTag;
	Payload.LayerTag = Def->ScreenLayerTag.IsValid()
		? Def->ScreenLayerTag
		: (Settings ? Settings->DefaultScreenLayerTag : FGameplayTag());
	Payload.OwningPhase = Phase;

	Bus->BroadcastPayload(FlowTags::Bus_ScreenPop, FInstancedStruct::Make(Payload), this);
}

void UFlow_GameFlowSubsystem::PushInputModeForPhase(const UFlow_FlowStateDefinition* Def, FGameplayTag Phase)
{
	// Compute the mode tag: phase definition override, else a phase-kind default.
	FGameplayTag ModeTag = (Def && Def->InputModeTag.IsValid()) ? Def->InputModeTag : DefaultInputModeForPhase(Phase);
	if (!ModeTag.IsValid())
	{
		return; // No input mode for this phase.
	}

	UObject* ArbiterObj = ResolveServiceObject(FlowTags::Service_InputModeArbiter);
	if (!ArbiterObj || !ArbiterObj->Implements<USeam_InputModeArbiter>())
	{
		UE_LOG(LogDP, Verbose, TEXT("[GameFlow] PushInputMode: no ISeam_InputModeArbiter provider; input mode inert."));
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const int32 Priority = Settings ? Settings->InputModePriority : 50; // Defensive default priority.

	ActiveInputModeRequest = ISeam_InputModeArbiter::Execute_PushInputMode(ArbiterObj, ModeTag, Priority);
	UE_LOG(LogDP, Verbose, TEXT("[GameFlow] Pushed input mode %s @prio %d (req %s)."),
		*ModeTag.ToString(), Priority, *ActiveInputModeRequest.ToString());
}

void UFlow_GameFlowSubsystem::PopInputMode()
{
	if (!ActiveInputModeRequest.IsValid())
	{
		return;
	}

	UObject* ArbiterObj = ResolveServiceObject(FlowTags::Service_InputModeArbiter);
	if (ArbiterObj && ArbiterObj->Implements<USeam_InputModeArbiter>())
	{
		ISeam_InputModeArbiter::Execute_PopInputMode(ArbiterObj, ActiveInputModeRequest);
	}

	ActiveInputModeRequest.Invalidate();
}

void UFlow_GameFlowSubsystem::SetGamePaused(bool bPause)
{
	// UGameplayStatics::SetGamePaused routes through the player controller; guard for a missing world/PC.
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGamePaused(World, bPause);
		UE_LOG(LogDP, Verbose, TEXT("[GameFlow] SetGamePaused(%s)."), bPause ? TEXT("true") : TEXT("false"));
	}
}

// ---------------------------------------------------------------------------------------------------
// Continue target
// ---------------------------------------------------------------------------------------------------

FString UFlow_GameFlowSubsystem::GetContinueTarget() const
{
	// An explicit override (player picked a slot) wins; otherwise derive from the save-slot seam.
	if (!ExplicitContinueSlot.IsEmpty())
	{
		return ExplicitContinueSlot;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	if (Settings && !Settings->bTrackContinueTarget)
	{
		return FString();
	}

	return ReadMostRecentSlot();
}

void UFlow_GameFlowSubsystem::SetContinueTarget(const FString& SlotName)
{
	ExplicitContinueSlot = SlotName;
	UE_LOG(LogDP, Verbose, TEXT("[GameFlow] Continue target set to '%s'."), *SlotName);
}

FString UFlow_GameFlowSubsystem::ReadMostRecentSlot() const
{
	UObject* SlotMgr = ResolveServiceObject(FlowTags::Service_SaveSlotManager);
	if (!SlotMgr || !SlotMgr->Implements<USeam_SaveSlotManager>())
	{
		return FString();
	}
	return ISeam_SaveSlotManager::Execute_GetMostRecentSlot(SlotMgr);
}

bool UFlow_GameFlowSubsystem::IsPaused() const
{
	return ActivePhase == FlowTags::Phase_Pause;
}

// ---------------------------------------------------------------------------------------------------
// Resolution helpers
// ---------------------------------------------------------------------------------------------------

const UFlow_FlowStateDefinition* UFlow_GameFlowSubsystem::ResolvePhaseDefinition(FGameplayTag Phase) const
{
	if (!Phase.IsValid())
	{
		return nullptr;
	}

	// Cache hit?
	if (const TObjectPtr<UFlow_FlowStateDefinition>* Cached = LoadedDefinitions.Find(Phase))
	{
		return Cached->Get();
	}

	UFlow_GameFlowSubsystem* MutableThis = const_cast<UFlow_GameFlowSubsystem*>(this);

	// 1) Settings PhaseDefinitions list (synchronously load the soft ref whose DataTag matches).
	if (const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get())
	{
		for (const TSoftObjectPtr<UFlow_FlowStateDefinition>& SoftDef : Settings->PhaseDefinitions)
		{
			if (SoftDef.IsNull())
			{
				continue;
			}
			// LoadSynchronous is acceptable here: phase defs are tiny metadata assets resolved at most
			// once per phase per session (then cached). The heavy level asset is a soft ref inside it.
			if (UFlow_FlowStateDefinition* Def = SoftDef.LoadSynchronous())
			{
				if (Def->DataTag == Phase)
				{
					MutableThis->LoadedDefinitions.Add(Phase, Def);
					return Def;
				}
			}
		}
	}

	// 2) Core data registry by DataTag.
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (UFlow_FlowStateDefinition* Def = Registry->Find<UFlow_FlowStateDefinition>(Phase))
		{
			MutableThis->LoadedDefinitions.Add(Phase, Def);
			return Def;
		}
	}

	// Cache the negative result as null so we don't re-scan every transition (entry exists, value null).
	MutableThis->LoadedDefinitions.Add(Phase, nullptr);
	UE_LOG(LogDP, Verbose, TEXT("[GameFlow] No phase definition authored for %s; using defensive defaults."),
		*Phase.ToString());
	return nullptr;
}

UDP_MessageBusSubsystem* UFlow_GameFlowSubsystem::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

UDP_ServiceLocatorSubsystem* UFlow_GameFlowSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

UObject* UFlow_GameFlowSubsystem::ResolveServiceObject(FGameplayTag Key) const
{
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	return Locator ? Locator->ResolveService(Key) : nullptr;
}

FGameplayTag UFlow_GameFlowSubsystem::DefaultInputModeForPhase(FGameplayTag Phase) const
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();

	// Pause is its own mode; active gameplay is the game mode; everything else is a menu/front-end mode.
	if (Phase == FlowTags::Phase_Pause)
	{
		return Settings ? Settings->PauseInputModeTag : FlowTags::InputMode_Pause;
	}
	if (Phase.MatchesTag(FlowTags::Phase_InGame))
	{
		return Settings ? Settings->GameInputModeTag : FlowTags::InputMode_Game;
	}
	// Boot has no UI/input; everything else (Title/MainMenu/Lobby/Loading/Results) is a menu mode.
	if (Phase == FlowTags::Phase_Boot)
	{
		return FGameplayTag();
	}
	return Settings ? Settings->MenuInputModeTag : FlowTags::InputMode_Menu;
}

APlayerController* UFlow_GameFlowSubsystem::GetLocalPlayerController() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	return GEngine ? GEngine->GetFirstLocalPlayerController(const_cast<UWorld*>(World)) : nullptr;
}

bool UFlow_GameFlowSubsystem::HasTravelAuthority() const
{
	const UWorld* World = GetWorld();
	// Standalone and listen-server / dedicated host can drive an authoritative OpenLevel; a pure client
	// cannot and must ClientTravel instead.
	return World && World->GetNetMode() != NM_Client;
}

// ---------------------------------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------------------------------

FString UFlow_GameFlowSubsystem::GetDPDebugString_Implementation() const
{
	const FString Continue = GetContinueTarget();
	return FString::Printf(TEXT("Flow: phase=%s prev=%s%s continue=%s svc=%s"),
		ActivePhase.IsValid() ? *ActivePhase.ToString() : TEXT("<none>"),
		PreviousPhase.IsValid() ? *PreviousPhase.ToString() : TEXT("<none>"),
		IsPaused() ? TEXT(" [paused]") : TEXT(""),
		Continue.IsEmpty() ? TEXT("<none>") : *Continue,
		bRegisteredAsService ? TEXT("registered") : TEXT("unregistered"));
}
