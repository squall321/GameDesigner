// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Menu/HUD_MenuStackSubsystem.h"

#include "HUD_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"
#include "Settings/HUD_DeveloperSettings.h"

#include "Mediator/DPUIManagerSubsystem.h"
#include "View/DPViewBase.h"

#include "Input/Seam_InputModeArbiter.h"

// Platform input device seam (soft) — used only to decide whether to drive gamepad focus.
#include "Input/UPlat_InputRouterSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4 and in CoreUObject on 5.5+ (payload decoding below).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UHUD_MenuStackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterBusListeners();
	UE_LOG(LogDP, Log, TEXT("HUD_MenuStackSubsystem initialized for LP '%s'."),
		*GetNameSafe(GetLocalPlayer()));
}

void UHUD_MenuStackSubsystem::Deinitialize()
{
	// Pop everything (also releases the input-mode lock) and detach bus listeners.
	PopToRoot();

	if (UDP_MessageBusSubsystem* Bus =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	Super::Deinitialize();
}

void UHUD_MenuStackSubsystem::RegisterBusListeners()
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	// "Back / cancel" intent -> pop the top menu screen.
	Bus->ListenNative(HUDTags::Bus_MenuBack,
		[this](const FDP_Message& /*Msg*/)
		{
			HandleBack();
		},
		this, EDP_MessageMatch::ExactOrChild);

	// "Push screen" intent -> push the screen named by the FHUD_MenuPushRequest payload.
	Bus->ListenNative(HUDTags::Bus_MenuPush,
		[this](const FDP_Message& Msg)
		{
			if (Msg.Payload.IsValid() &&
				Msg.Payload.GetScriptStruct() == FHUD_MenuPushRequest::StaticStruct())
			{
				const FHUD_MenuPushRequest& Req = Msg.Payload.Get<FHUD_MenuPushRequest>();
				if (Req.ScreenTag.IsValid())
				{
					PushMenu(Req.ScreenTag, /*ViewModel=*/nullptr);
				}
			}
		},
		this, EDP_MessageMatch::ExactOrChild);
}

UDP_UIManagerSubsystem* UHUD_MenuStackSubsystem::GetUIManager() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_UIManagerSubsystem>(this);
}

FGameplayTag UHUD_MenuStackSubsystem::ResolveMenuLayerTag() const
{
	if (const UHUD_DeveloperSettings* Settings = UHUD_DeveloperSettings::Get())
	{
		if (Settings->DefaultMenuLayerTag.IsValid())
		{
			return Settings->DefaultMenuLayerTag;
		}
	}
	// Defensive fallback to the conventional core menu layer when settings leave it unset. Documented
	// fallback constant (a tag), not a gameplay magic number.
	return FGameplayTag::RequestGameplayTag(FName(TEXT("DP.UI.Layer.Menu")), /*ErrorIfNotFound=*/false);
}

UDP_ViewBase* UHUD_MenuStackSubsystem::PushMenu(FGameplayTag ScreenTag, UDP_ViewModelBase* ViewModel)
{
	if (!ScreenTag.IsValid())
	{
		return nullptr;
	}

	UDP_UIManagerSubsystem* UI = GetUIManager();
	if (!UI)
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_MenuStack::PushMenu: no UI mediator available."));
		return nullptr;
	}

	// Realise the widget through the core mediator onto the menu layer for this local player.
	UDP_ViewBase* View = UI->PushScreen(ScreenTag, ViewModel, GetLocalPlayer());
	if (!View)
	{
		UE_LOG(LogDP, Warning, TEXT("HUD_MenuStack::PushMenu: mediator could not push screen '%s'."),
			*ScreenTag.ToString());
		return nullptr;
	}

	// First screen: acquire the input-mode lock so opening a menu hands input ownership to the UI.
	if (ScreenHistory.Num() == 0)
	{
		AcquireInputModeLock();
	}

	ScreenHistory.Add(ScreenTag);
	FocusWidgetForGamepad(View);

	OnMenuStackChanged.Broadcast(ScreenHistory.Num());
	UE_LOG(LogDP, Verbose, TEXT("HUD_MenuStack: pushed '%s' (depth=%d)."),
		*ScreenTag.ToString(), ScreenHistory.Num());
	return View;
}

bool UHUD_MenuStackSubsystem::PopMenu()
{
	if (ScreenHistory.Num() == 0)
	{
		return false;
	}

	const FGameplayTag Top = ScreenHistory.Last();
	const FGameplayTag MenuLayer = ResolveMenuLayerTag();

	if (UDP_UIManagerSubsystem* UI = GetUIManager())
	{
		UI->PopScreen(MenuLayer, GetLocalPlayer());
	}

	ScreenHistory.Pop();

	// Last screen closed: release the input-mode lock. (The newly-exposed top screen, if any, keeps the
	// focus the engine restores to the next widget in the viewport's focus chain; the mediator owns those
	// widget instances, so this subsystem does not re-grab a handle to force-focus on pop.)
	if (ScreenHistory.Num() == 0)
	{
		ReleaseInputModeLock();
	}

	OnMenuStackChanged.Broadcast(ScreenHistory.Num());
	UE_LOG(LogDP, Verbose, TEXT("HUD_MenuStack: popped '%s' (depth=%d)."),
		*Top.ToString(), ScreenHistory.Num());
	return true;
}

int32 UHUD_MenuStackSubsystem::PopToRoot()
{
	int32 Popped = 0;
	while (ScreenHistory.Num() > 0)
	{
		if (!PopMenu())
		{
			break;
		}
		++Popped;
	}
	return Popped;
}

bool UHUD_MenuStackSubsystem::HandleBack()
{
	// One back step = pop the top screen. Returns false (back not consumed) when no menu is open, so a
	// caller can fall through to e.g. opening the pause menu.
	if (ScreenHistory.Num() == 0)
	{
		return false;
	}
	return PopMenu();
}

void UHUD_MenuStackSubsystem::AcquireInputModeLock()
{
	if (bHoldingInputMode)
	{
		return;
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* ArbiterObj = Locator->ResolveService(HUDTags::Service_InputModeArbiter);
	if (!ArbiterObj || !ArbiterObj->Implements<USeam_InputModeArbiter>())
	{
		// No arbiter registered (e.g. Platform module absent): degrade gracefully — the menu still works,
		// it just does not take over the input mode.
		UE_LOG(LogDP, Verbose, TEXT("HUD_MenuStack: no input-mode arbiter; menu opens without input lock."));
		return;
	}

	const UHUD_DeveloperSettings* Settings = UHUD_DeveloperSettings::Get();
	const FGameplayTag ModeTag = (Settings && Settings->MenuInputModeTag.IsValid())
		? Settings->MenuInputModeTag : HUDTags::InputMode_Menu;
	const int32 Priority = Settings ? Settings->MenuInputModePriority : 100;

	InputModeRequestId = ISeam_InputModeArbiter::Execute_PushInputMode(ArbiterObj, ModeTag, Priority);
	bHoldingInputMode = InputModeRequestId.IsValid();
}

void UHUD_MenuStackSubsystem::ReleaseInputModeLock()
{
	if (!bHoldingInputMode)
	{
		return;
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		UObject* ArbiterObj = Locator->ResolveService(HUDTags::Service_InputModeArbiter);
		if (ArbiterObj && ArbiterObj->Implements<USeam_InputModeArbiter>())
		{
			ISeam_InputModeArbiter::Execute_PopInputMode(ArbiterObj, InputModeRequestId);
		}
	}

	InputModeRequestId.Invalidate();
	bHoldingInputMode = false;
}

void UHUD_MenuStackSubsystem::FocusWidgetForGamepad(UDP_ViewBase* Widget)
{
	if (!Widget)
	{
		return;
	}

	// Only drive focus for gamepad players — keyboard/mouse focus is pointer-driven. Soft Platform lookup.
	const UPlat_InputRouterSubsystem* Router =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UPlat_InputRouterSubsystem>(this);
	if (Router && !Router->IsGamepadActive())
	{
		return;
	}

	const ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(LP->GetWorld()) : nullptr;
	if (PC)
	{
		// Hand keyboard/controller focus to the widget so the gamepad lands on a focusable element.
		Widget->SetUserFocus(PC);
	}
}
