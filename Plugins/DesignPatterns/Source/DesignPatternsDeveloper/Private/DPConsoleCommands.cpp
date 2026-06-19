// Copyright DesignPatterns plugin. All Rights Reserved.

// All DP.* console commands. Each command body is compiled out of Shipping builds; the module
// itself is a normal Runtime module so the commands survive into Test builds (for QA with cheats).
#if !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "UObject/UObjectIterator.h"

#include "Core/DPLog.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Pool/DPObjectPoolSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "FSM/DPStateMachineComponent.h"
#include "Command/DPCommandHistorySubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Save/DPSaveGameSubsystem.h"
#include "Save/DPSaveHeader.h"

namespace DPConsole
{
	/** Resolve a GameInstance subsystem from a console-provided world. */
	template <typename T>
	static T* GI(UWorld* World)
	{
		if (World && World->GetGameInstance())
		{
			return World->GetGameInstance()->GetSubsystem<T>();
		}
		return nullptr;
	}

	/** Resolve a World subsystem from a console-provided world. */
	template <typename T>
	static T* WS(UWorld* World)
	{
		return World ? World->GetSubsystem<T>() : nullptr;
	}

	/** Parse the first argument as a gameplay tag, or return an empty tag. */
	static FGameplayTag ParseTag(const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			return FGameplayTag();
		}
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false);
	}
}

// ============================================================================================
// Message Bus
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPBusDumpListeners(
	TEXT("DP.Bus.DumpListeners"),
	TEXT("Dump every message-bus channel and its listeners (owner names)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_MessageBusSubsystem* Bus = DPConsole::GI<UDP_MessageBusSubsystem>(World))
		{
			Bus->DumpListeners();
			UE_LOG(LogDPBus, Log, TEXT("DP.Bus: %d total listeners."), Bus->GetListenerCount());
		}
		else
		{
			UE_LOG(LogDPBus, Warning, TEXT("DP.Bus.DumpListeners: no message bus in this world."));
		}
	}));

// ============================================================================================
// Object Pool
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPPoolStats(
	TEXT("DP.Pool.Stats"),
	TEXT("Log object-pool occupancy for the current world."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_ObjectPoolSubsystem* Pool = DPConsole::WS<UDP_ObjectPoolSubsystem>(World))
		{
			UE_LOG(LogDPPool, Log, TEXT("DP.Pool: %s"), *Pool->GetDPDebugString());
		}
		else
		{
			UE_LOG(LogDPPool, Warning, TEXT("DP.Pool.Stats: no object pool in this world."));
		}
	}));

// ============================================================================================
// FSM
// ============================================================================================
static FAutoConsoleCommandWithWorldAndArgs GDPFSMLogState(
	TEXT("DP.FSM.LogState"),
	TEXT("Log the active state of every UDP_StateMachineComponent (optional substring filter on owner name)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const FString Filter = Args.Num() > 0 ? Args[0] : FString();
		int32 Count = 0;
		for (TObjectIterator<UDP_StateMachineComponent> It; It; ++It)
		{
			UDP_StateMachineComponent* Comp = *It;
			if (!Comp || Comp->GetWorld() != World)
			{
				continue;
			}
			const FString OwnerName = GetNameSafe(Comp->GetOwner());
			if (!Filter.IsEmpty() && !OwnerName.Contains(Filter))
			{
				continue;
			}
			UE_LOG(LogDPFSM, Log, TEXT("DP.FSM [%s]: %s"), *OwnerName, *Comp->GetDebugString());
			++Count;
		}
		UE_LOG(LogDPFSM, Log, TEXT("DP.FSM.LogState: %d component(s) reported."), Count);
	}));

// ============================================================================================
// Service Locator
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPServiceList(
	TEXT("DP.Service.List"),
	TEXT("Dump every registered service (key -> provider)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_ServiceLocatorSubsystem* Loc = DPConsole::GI<UDP_ServiceLocatorSubsystem>(World))
		{
			Loc->DumpServices();
		}
		else
		{
			UE_LOG(LogDPService, Warning, TEXT("DP.Service.List: no service locator in this world."));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GDPServiceResolve(
	TEXT("DP.Service.Resolve"),
	TEXT("Resolve a service by tag. Usage: DP.Service.Resolve DP.Service.MyService"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag Key = DPConsole::ParseTag(Args);
		if (!Key.IsValid())
		{
			UE_LOG(LogDPService, Warning, TEXT("DP.Service.Resolve: pass a valid tag, e.g. DP.Service.MyService."));
			return;
		}
		if (UDP_ServiceLocatorSubsystem* Loc = DPConsole::GI<UDP_ServiceLocatorSubsystem>(World))
		{
			UObject* Provider = Loc->ResolveService(Key);
			UE_LOG(LogDPService, Log, TEXT("DP.Service.Resolve '%s' -> %s"),
				*Key.ToString(), Provider ? *Provider->GetName() : TEXT("<none>"));
		}
	}));

// ============================================================================================
// Command history
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPCmdDump(
	TEXT("DP.Cmd.Dump"),
	TEXT("Dump the command history (undo/redo ring)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* Hist = DPConsole::WS<UDP_CommandHistorySubsystem>(World))
		{
			Hist->DumpHistory();
		}
		else
		{
			UE_LOG(LogDPCmd, Warning, TEXT("DP.Cmd.Dump: no command history in this world."));
		}
	}));

static FAutoConsoleCommandWithWorld GDPCmdUndo(
	TEXT("DP.Cmd.Undo"),
	TEXT("Undo the last undoable command."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* Hist = DPConsole::WS<UDP_CommandHistorySubsystem>(World))
		{
			UE_LOG(LogDPCmd, Log, TEXT("DP.Cmd.Undo -> %s"), Hist->Undo() ? TEXT("ok") : TEXT("nothing to undo"));
		}
	}));

static FAutoConsoleCommandWithWorld GDPCmdRedo(
	TEXT("DP.Cmd.Redo"),
	TEXT("Redo the last undone command."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* Hist = DPConsole::WS<UDP_CommandHistorySubsystem>(World))
		{
			UE_LOG(LogDPCmd, Log, TEXT("DP.Cmd.Redo -> %s"), Hist->Redo() ? TEXT("ok") : TEXT("nothing to redo"));
		}
	}));

// ============================================================================================
// Data registry
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPDataRebuild(
	TEXT("DP.Data.RebuildIndex"),
	TEXT("Rebuild the data registry index from the asset registry."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_DataRegistrySubsystem* Reg = DPConsole::GI<UDP_DataRegistrySubsystem>(World))
		{
			Reg->RebuildIndex();
			UE_LOG(LogDPData, Log, TEXT("DP.Data.RebuildIndex: %d tag(s) indexed."), Reg->ListTags().Num());
		}
	}));

static FAutoConsoleCommandWithWorld GDPDataListTags(
	TEXT("DP.Data.ListTags"),
	TEXT("List every data tag in the registry index."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_DataRegistrySubsystem* Reg = DPConsole::GI<UDP_DataRegistrySubsystem>(World))
		{
			const TArray<FGameplayTag> Tags = Reg->ListTags();
			UE_LOG(LogDPData, Log, TEXT("DP.Data: %d tag(s):"), Tags.Num());
			for (const FGameplayTag& Tag : Tags)
			{
				UE_LOG(LogDPData, Log, TEXT("  %s"), *Tag.ToString());
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GDPDataResolve(
	TEXT("DP.Data.Resolve"),
	TEXT("Resolve (synchronously load) a data asset by tag. Usage: DP.Data.Resolve DP.Data.Sword"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag Tag = DPConsole::ParseTag(Args);
		if (!Tag.IsValid())
		{
			UE_LOG(LogDPData, Warning, TEXT("DP.Data.Resolve: pass a valid tag."));
			return;
		}
		if (UDP_DataRegistrySubsystem* Reg = DPConsole::GI<UDP_DataRegistrySubsystem>(World))
		{
			const UObject* Asset = Reg->FindByTag(Tag);
			UE_LOG(LogDPData, Log, TEXT("DP.Data.Resolve '%s' -> %s"),
				*Tag.ToString(), Asset ? *Asset->GetName() : TEXT("<not found>"));
		}
	}));

// ============================================================================================
// Save system
// ============================================================================================
static FAutoConsoleCommandWithWorld GDPSaveListSlots(
	TEXT("DP.Save.ListSlots"),
	TEXT("List every save slot on disk."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (UDP_SaveGameSubsystem* Save = DPConsole::GI<UDP_SaveGameSubsystem>(World))
		{
			const TArray<FString> Slots = Save->GetAllSlots();
			UE_LOG(LogDPSave, Log, TEXT("DP.Save: %d slot(s):"), Slots.Num());
			for (const FString& Slot : Slots)
			{
				UE_LOG(LogDPSave, Log, TEXT("  %s"), *Slot);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GDPSaveDeleteSlot(
	TEXT("DP.Save.DeleteSlot"),
	TEXT("Delete a save slot. Usage: DP.Save.DeleteSlot <slot>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogDPSave, Warning, TEXT("DP.Save.DeleteSlot: pass a slot name."));
			return;
		}
		if (UDP_SaveGameSubsystem* Save = DPConsole::GI<UDP_SaveGameSubsystem>(World))
		{
			const bool bOk = Save->DeleteSlot(Args[0]);
			UE_LOG(LogDPSave, Log, TEXT("DP.Save.DeleteSlot '%s' -> %s"), *Args[0], bOk ? TEXT("deleted") : TEXT("not found"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GDPSaveDumpHeader(
	TEXT("DP.Save.DumpHeader"),
	TEXT("Read and log a slot's header metadata. Usage: DP.Save.DumpHeader <slot>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() == 0)
		{
			UE_LOG(LogDPSave, Warning, TEXT("DP.Save.DumpHeader: pass a slot name."));
			return;
		}
		if (UDP_SaveGameSubsystem* Save = DPConsole::GI<UDP_SaveGameSubsystem>(World))
		{
			FDP_SaveHeader Header;
			if (Save->ReadSlotHeader(Args[0], Header))
			{
				UE_LOG(LogDPSave, Log,
					TEXT("DP.Save.DumpHeader '%s': version=%d class=%s saved=%s playtime=%.1fs display='%s'"),
					*Args[0], Header.SaveVersion, *Header.SaveGameClassPath,
					*Header.TimestampUtc.ToString(), Header.PlaytimeSeconds, *Header.DisplayName);
			}
			else
			{
				UE_LOG(LogDPSave, Warning, TEXT("DP.Save.DumpHeader '%s': missing or corrupt."), *Args[0]);
			}
		}
	}));

#endif // !UE_BUILD_SHIPPING
