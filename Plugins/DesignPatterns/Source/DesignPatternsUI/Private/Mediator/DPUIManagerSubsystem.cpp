// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mediator/DPUIManagerSubsystem.h"
#include "Mediator/DPUIRegistryDataAsset.h"
#include "Mediator/DPUILayoutSubsystem.h"
#include "Mediator/DPLayerStack.h"
#include "View/DPViewBase.h"
#include "MVVM/DPViewModelBase.h"
#include "Core/DPLog.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "HAL/IConsoleManager.h"

DECLARE_CYCLE_STAT(TEXT("DP UI PushScreen"), STAT_DP_UI_PushScreen, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("DP UI Screens Pushed"), STAT_DP_UI_ScreensPushed, STATGROUP_DesignPatterns);

void UDP_UIManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The per-local-player UDP_UILayoutSubsystem is created automatically by the engine
	// when each local player spawns; this GameInstance-scoped mediator only routes to it.
	UE_LOG(LogDP, Log, TEXT("[UI] Manager (mediator) subsystem initialized."));

	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("DP.UI.DumpStack"),
		TEXT("Dump every UI layer stack (per local player) to the log."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			DumpStackToLog();
		}),
		ECVF_Default));
}

void UDP_UIManagerSubsystem::Deinitialize()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* Command : ConsoleCommands)
	{
		if (Command)
		{
			ConsoleManager.UnregisterConsoleObject(Command);
		}
	}
	ConsoleCommands.Reset();

	Registry = nullptr;
	Super::Deinitialize();
}

FString UDP_UIManagerSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("UIManager: registry=%s"),
		Registry ? *Registry->GetName() : TEXT("<none>"));
}

void UDP_UIManagerSubsystem::SetRegistry(UDP_UIRegistryDataAsset* InRegistry)
{
	Registry = InRegistry;
	UE_LOG(LogDP, Log, TEXT("[UI] Registry set to '%s'."),
		Registry ? *Registry->GetName() : TEXT("<null>"));
}

ULocalPlayer* UDP_UIManagerSubsystem::GetFirstLocalPlayer() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetFirstGamePlayer() : nullptr;
}

UDP_UILayoutSubsystem* UDP_UIManagerSubsystem::GetLayoutFor(ULocalPlayer* LocalPlayer) const
{
	ULocalPlayer* Target = LocalPlayer ? LocalPlayer : GetFirstLocalPlayer();
	if (!Target)
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] No local player available to resolve a layout subsystem."));
		return nullptr;
	}
	return Target->GetSubsystem<UDP_UILayoutSubsystem>();
}

UDP_ViewBase* UDP_UIManagerSubsystem::CreateViewForScreen(const FDP_ScreenDef& ScreenDef, ULocalPlayer* OwningPlayer)
{
	// Synchronous load of the soft widget class. Games wanting async should preload via the asset manager.
	TSubclassOf<UDP_ViewBase> WidgetClass = ScreenDef.WidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		UE_LOG(LogDP, Error, TEXT("[UI] Screen '%s' has no loadable WidgetClass."),
			*ScreenDef.ScreenTag.ToString());
		return nullptr;
	}

	// Prefer owning the widget to the local player's PlayerController (correct input routing &
	// per-player ownership); fall back to the game instance if no PC exists yet.
	UDP_ViewBase* View = nullptr;
	APlayerController* PC = OwningPlayer ? OwningPlayer->GetPlayerController(OwningPlayer->GetWorld()) : nullptr;
	if (PC)
	{
		View = CreateWidget<UDP_ViewBase>(PC, WidgetClass);
	}
	else if (UGameInstance* GI = GetGameInstance())
	{
		View = CreateWidget<UDP_ViewBase>(GI, WidgetClass);
	}

	if (!View)
	{
		UE_LOG(LogDP, Error, TEXT("[UI] CreateWidget failed for screen '%s'."),
			*ScreenDef.ScreenTag.ToString());
	}
	return View;
}

UDP_ViewBase* UDP_UIManagerSubsystem::PushScreen(FGameplayTag ScreenTag, UDP_ViewModelBase* ViewModel,
	ULocalPlayer* LocalPlayer)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_UI_PushScreen);

	if (!Registry)
	{
		UE_LOG(LogDP, Error, TEXT("[UI] PushScreen('%s') failed: no registry set."),
			*ScreenTag.ToString());
		return nullptr;
	}

	const FDP_ScreenDef* ScreenDef = Registry->FindScreen(ScreenTag);
	if (!ScreenDef)
	{
		UE_LOG(LogDP, Error, TEXT("[UI] PushScreen('%s') failed: screen not in registry."),
			*ScreenTag.ToString());
		return nullptr;
	}

	ULocalPlayer* OwningPlayer = LocalPlayer ? LocalPlayer : GetFirstLocalPlayer();
	UDP_UILayoutSubsystem* Layout = GetLayoutFor(OwningPlayer);
	if (!Layout)
	{
		return nullptr;
	}

	UDP_LayerStack* Layer = Layout->GetOrCreateLayer(ScreenDef->LayerTag);
	if (!Layer)
	{
		UE_LOG(LogDP, Error, TEXT("[UI] PushScreen('%s') failed: invalid layer '%s'."),
			*ScreenTag.ToString(), *ScreenDef->LayerTag.ToString());
		return nullptr;
	}

	UDP_ViewBase* View = CreateViewForScreen(*ScreenDef, OwningPlayer);
	if (!View)
	{
		return nullptr;
	}

	if (ViewModel)
	{
		View->SetViewModel(ViewModel);
	}

	if (!Layer->Push(ScreenTag, View, ScreenDef->ZOrder))
	{
		return nullptr;
	}

	INC_DWORD_STAT(STAT_DP_UI_ScreensPushed);
	UE_LOG(LogDP, Log, TEXT("[UI] Pushed screen '%s' onto layer '%s'."),
		*ScreenTag.ToString(), *ScreenDef->LayerTag.ToString());
	return View;
}

bool UDP_UIManagerSubsystem::PopScreen(FGameplayTag LayerTag, ULocalPlayer* LocalPlayer)
{
	UDP_UILayoutSubsystem* Layout = GetLayoutFor(LocalPlayer);
	if (!Layout)
	{
		return false;
	}

	UDP_LayerStack* Layer = Layout->FindLayer(LayerTag);
	if (!Layer)
	{
		UE_LOG(LogDP, Verbose, TEXT("[UI] PopScreen: layer '%s' does not exist."), *LayerTag.ToString());
		return false;
	}
	return Layer->Pop();
}

bool UDP_UIManagerSubsystem::PopToScreen(FGameplayTag ScreenTag, ULocalPlayer* LocalPlayer)
{
	if (!Registry)
	{
		return false;
	}

	const FDP_ScreenDef* ScreenDef = Registry->FindScreen(ScreenTag);
	if (!ScreenDef)
	{
		UE_LOG(LogDP, Warning, TEXT("[UI] PopToScreen('%s'): not in registry."), *ScreenTag.ToString());
		return false;
	}

	UDP_UILayoutSubsystem* Layout = GetLayoutFor(LocalPlayer);
	if (!Layout)
	{
		return false;
	}

	UDP_LayerStack* Layer = Layout->FindLayer(ScreenDef->LayerTag);
	return Layer ? Layer->PopToTag(ScreenTag) : false;
}

FGameplayTag UDP_UIManagerSubsystem::GetTopScreen(FGameplayTag LayerTag, ULocalPlayer* LocalPlayer) const
{
	UDP_UILayoutSubsystem* Layout = GetLayoutFor(LocalPlayer);
	if (!Layout)
	{
		return FGameplayTag();
	}
	UDP_LayerStack* Layer = Layout->FindLayer(LayerTag);
	return Layer ? Layer->GetTopScreenTag() : FGameplayTag();
}

FString UDP_UIManagerSubsystem::BuildStackDump() const
{
	TArray<FString> Lines;
	Lines.Add(TEXT("=== DesignPatterns UI Stack Dump ==="));

	const UGameInstance* GI = GetGameInstance();
	if (GI)
	{
		bool bAny = false;
		for (ULocalPlayer* LP : GI->GetLocalPlayers())
		{
			if (!LP)
			{
				continue;
			}
			if (UDP_UILayoutSubsystem* Layout = LP->GetSubsystem<UDP_UILayoutSubsystem>())
			{
				Layout->DumpTo(Lines);
				bAny = true;
			}
		}
		if (!bAny)
		{
			Lines.Add(TEXT("  (no layout subsystems)"));
		}
	}

	Lines.Add(FString::Printf(TEXT("Registry: %s"),
		Registry ? *Registry->GetName() : TEXT("<none>")));

	return FString::Join(Lines, TEXT("\n"));
}

void UDP_UIManagerSubsystem::DumpStackToLog() const
{
	UE_LOG(LogDP, Log, TEXT("\n%s"), *BuildStackDump());
}
