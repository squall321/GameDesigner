// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Autosave/SaveX_AutosaveSubsystem.h"

#include "Settings/SaveX_DeveloperSettings.h"
#include "SaveX_ServiceKeys.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Save/DPSaveGame.h"
#include "Save/DPSaveGameSubsystem.h"

#include "Persist/Seam_SaveSlotManager.h"

#include "Misc/App.h"

namespace
{
	/** Human-readable reason label for logs/debug string. */
	const TCHAR* ReasonToString(ESaveX_AutosaveReason Reason)
	{
		switch (Reason)
		{
		case ESaveX_AutosaveReason::Interval:   return TEXT("Interval");
		case ESaveX_AutosaveReason::BusEvent:   return TEXT("BusEvent");
		case ESaveX_AutosaveReason::Checkpoint: return TEXT("Checkpoint");
		case ESaveX_AutosaveReason::Manual:     return TEXT("Manual");
		default:                                return TEXT("Unknown");
		}
	}
}

void USaveX_AutosaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache the sibling message bus (valid for the GI lifetime; held weakly so a GI teardown order quirk
	// cannot dangle).
	MessageBus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);

	// Subscribe to settings-configured bus channels that should trigger an autosave.
	SubscribeToTriggerChannels();

	// Start the periodic interval driver (skipped when the interval is non-positive).
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const float Interval = Settings ? Settings->AutosaveIntervalSeconds : 300.f /*defensive fallback*/;
	if (Interval > 0.f)
	{
		IntervalTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &USaveX_AutosaveSubsystem::TickInterval), Interval);
	}

	// Seed the ring cursor from existing slots so rotation continues correctly across a load/level travel.
	SeedRingCursorFromSlots();

	UE_LOG(LogDPSave, Log, TEXT("[Autosave] Initialized (ringSize=%d, interval=%.1fs)."),
		Settings ? Settings->GetEffectiveAutosaveRingSize() : 3, Interval);
}

void USaveX_AutosaveSubsystem::Deinitialize()
{
	// Remove the interval ticker so it cannot fire into a torn-down subsystem.
	if (IntervalTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(IntervalTickerHandle);
		IntervalTickerHandle.Reset();
	}

	// Drop all bus listeners we own.
	if (UDP_MessageBusSubsystem* Bus = MessageBus.Get())
	{
		Bus->StopListeningForOwner(this);
	}
	BusListenerIds.Reset();

	Super::Deinitialize();
}

bool USaveX_AutosaveSubsystem::RequestAutosave(ESaveX_AutosaveReason Reason)
{
	UDP_SaveGameSubsystem* SaveSubsystem = ResolveSaveSubsystem();
	if (!SaveSubsystem)
	{
		// Inert default: no save backend -> nothing to do.
		UE_LOG(LogDPSave, Verbose, TEXT("[Autosave] Request (%s) dropped: no save subsystem."),
			ReasonToString(Reason));
		return false;
	}

	// THROTTLE: collapse bursts of triggers into a single write within the min-interval window.
	if (!CanAutosaveNow())
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Autosave] Request (%s) throttled."), ReasonToString(Reason));
		return false;
	}

	const FString SlotName = GetNextRingSlotName();
	if (SlotName.IsEmpty())
	{
		return false;
	}

	UDP_SaveGame* SaveObject = BuildAutosaveObject();
	if (!SaveObject)
	{
		UE_LOG(LogDPSave, Warning, TEXT("[Autosave] Could not build save object; request (%s) aborted."),
			ReasonToString(Reason));
		return false;
	}

	// Record the throttle timestamp and reason now (the write is async; we throttle on request time).
	LastAutosaveTime = FApp::GetCurrentTime();
	LastReason = Reason;

	TWeakObjectPtr<USaveX_AutosaveSubsystem> WeakThis(this);
	const int32 WrittenRingIndex = RingCursor;

	FDP_SaveCallbackDynamic OnComplete;
	OnComplete.BindWeakLambda(this, [WeakThis](FString Slot, EDP_SaveResult Result)
	{
		if (USaveX_AutosaveSubsystem* Self = WeakThis.Get())
		{
			const bool bOk = (Result == EDP_SaveResult::Success);
			if (bOk)
			{
				++Self->AutosaveCount;
			}
			else
			{
				UE_LOG(LogDPSave, Warning, TEXT("[Autosave] Write to '%s' failed (result=%d)."),
					*Slot, static_cast<int32>(Result));
			}
			Self->OnAutosaveCompleted.Broadcast(Slot, bOk);
		}
	});

	SaveSubsystem->SaveAsync(SlotName, SaveObject, OnComplete);

	// Advance the ring cursor so the NEXT autosave targets the following (oldest) slot.
	const int32 RingSize = USaveX_DeveloperSettings::Get()
		? USaveX_DeveloperSettings::Get()->GetEffectiveAutosaveRingSize() : 3;
	RingCursor = (WrittenRingIndex + 1) % FMath::Max(1, RingSize);

	UE_LOG(LogDPSave, Log, TEXT("[Autosave] Writing slot '%s' (reason=%s)."),
		*SlotName, ReasonToString(Reason));
	return true;
}

bool USaveX_AutosaveSubsystem::CanAutosaveNow() const
{
	if (!ResolveSaveSubsystem())
	{
		return false;
	}
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const float MinInterval = Settings ? Settings->GetEffectiveAutosaveMinInterval() : 30.f /*defensive*/;
	if (MinInterval <= 0.f)
	{
		return true; // No throttle configured.
	}
	const double Now = FApp::GetCurrentTime();
	return (Now - LastAutosaveTime) >= static_cast<double>(MinInterval);
}

FString USaveX_AutosaveSubsystem::GetNextRingSlotName() const
{
	return RingSlotNameForIndex(RingCursor);
}

void USaveX_AutosaveSubsystem::GetRingSlotNames(TArray<FString>& OutSlots) const
{
	OutSlots.Reset();
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;
	OutSlots.Reserve(RingSize);
	for (int32 Index = 0; Index < RingSize; ++Index)
	{
		OutSlots.Add(RingSlotNameForIndex(Index));
	}
}

FString USaveX_AutosaveSubsystem::GetDPDebugString_Implementation() const
{
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;
	const bool bReady = CanAutosaveNow();
	return FString::Printf(TEXT("Autosave: count=%d ring=%d nextSlot=%s lastReason=%s ready=%s"),
		AutosaveCount, RingSize, *GetNextRingSlotName(), ReasonToString(LastReason),
		bReady ? TEXT("yes") : TEXT("no"));
}

void USaveX_AutosaveSubsystem::SubscribeToTriggerChannels()
{
	UDP_MessageBusSubsystem* Bus = MessageBus.Get();
	if (!Bus)
	{
		return;
	}
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}

	TWeakObjectPtr<USaveX_AutosaveSubsystem> WeakThis(this);
	for (const FGameplayTag& Channel : Settings->AutosaveTriggerChannels)
	{
		if (!Channel.IsValid())
		{
			continue;
		}
		const FDP_ListenerHandle Handle = Bus->ListenNative(
			Channel,
			[WeakThis](const FDP_Message& /*Message*/)
			{
				if (USaveX_AutosaveSubsystem* Self = WeakThis.Get())
				{
					Self->RequestAutosave(ESaveX_AutosaveReason::BusEvent);
				}
			},
			/*OwnerForLifetime=*/this);
		BusListenerIds.Add(Handle.Id);
	}

	UE_CLOG(BusListenerIds.Num() > 0, LogDPSave, Log,
		TEXT("[Autosave] Subscribed to %d trigger channel(s)."), BusListenerIds.Num());
}

bool USaveX_AutosaveSubsystem::TickInterval(float /*DeltaTime*/)
{
	RequestAutosave(ESaveX_AutosaveReason::Interval);
	return true; // Keep the ticker alive; removed explicitly on Deinitialize.
}

TScriptInterface<ISeam_SaveSlotManager> USaveX_AutosaveSubsystem::ResolveSlotManager() const
{
	TScriptInterface<ISeam_SaveSlotManager> Result;

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Result;
	}

	FGameplayTag Key;
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		Key = Settings->SlotManagerServiceTag;
	}
	if (!Key.IsValid())
	{
		Key = SaveX_ServiceKeys::SlotManager();
	}
	if (!Key.IsValid())
	{
		return Result;
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_SaveSlotManager::StaticClass()))
	{
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ISeam_SaveSlotManager>(Provider));
	}
	return Result;
}

UDP_SaveGameSubsystem* USaveX_AutosaveSubsystem::ResolveSaveSubsystem() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_SaveGameSubsystem>(
		const_cast<USaveX_AutosaveSubsystem*>(this));
}

FString USaveX_AutosaveSubsystem::RingSlotNameForIndex(int32 Index) const
{
	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const FString Prefix = (Settings && !Settings->AutosaveSlotPrefix.IsEmpty())
		? Settings->AutosaveSlotPrefix
		: TEXT("DPAutosave") /*defensive fallback*/;
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;
	const int32 Wrapped = ((Index % RingSize) + RingSize) % RingSize;
	return FString::Printf(TEXT("%s_%d"), *Prefix, Wrapped);
}

void USaveX_AutosaveSubsystem::SeedRingCursorFromSlots()
{
	// If a slot manager is available, point the cursor at the slot AFTER the most-recently-written ring slot
	// so the next write naturally overwrites the oldest, even after level travel restarts this subsystem.
	TScriptInterface<ISeam_SaveSlotManager> SlotManager = ResolveSlotManager();
	if (!SlotManager)
	{
		RingCursor = 0;
		return;
	}

	const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get();
	const int32 RingSize = Settings ? Settings->GetEffectiveAutosaveRingSize() : 3;

	const FString MostRecent = ISeam_SaveSlotManager::Execute_GetMostRecentSlot(SlotManager.GetObject());
	for (int32 Index = 0; Index < RingSize; ++Index)
	{
		if (RingSlotNameForIndex(Index) == MostRecent)
		{
			RingCursor = (Index + 1) % FMath::Max(1, RingSize);
			return;
		}
	}
	RingCursor = 0;
}

UDP_SaveGame* USaveX_AutosaveSubsystem::BuildAutosaveObject() const
{
	TSubclassOf<UDP_SaveGame> SaveClass = UDP_SaveGame::StaticClass();
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		if (UClass* Resolved = Settings->SaveGameClass.LoadSynchronous())
		{
			SaveClass = Resolved;
		}
	}

	UDP_SaveGame* SaveObject = NewObject<UDP_SaveGame>(GetTransientPackage(), SaveClass);
	if (SaveObject)
	{
		SaveObject->DisplayName = TEXT("Autosave");
	}
	return SaveObject;
}
