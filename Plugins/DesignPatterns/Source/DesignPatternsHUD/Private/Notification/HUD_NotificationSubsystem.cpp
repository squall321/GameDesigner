// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Notification/HUD_NotificationSubsystem.h"
#include "Notification/HUD_NotificationViewModel.h"
#include "Notification/HUD_NotificationTypes.h"
#include "Data/HUD_NotificationMapDataAsset.h"
#include "Seam/HUD_NotificationSource.h"
#include "HUD_HudNotifyTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

//==================================== Lifecycle =====================================

void UHUD_NotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The ViewModel is owned for the life of the subsystem; the UI binds to it directly.
	ViewModel = NewObject<UHUD_NotificationViewModel>(this);

	// Always listen on the HUD direct-request channels (notify/dismiss) regardless of any map.
	RefreshBusSubscriptions();

	// Drive auto-dismiss timers once per frame. The subsystem is not an FTickableGameObject to avoid
	// ticking in editor/inactive worlds; FTSTicker is paused with the game loop naturally.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UHUD_NotificationSubsystem::TickNotifications));

	PushToViewModel();

	UE_LOG(LogDP, Verbose, TEXT("[HUD] NotificationSubsystem initialized for local player."));
}

void UHUD_NotificationSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}

	// Detach all sources.
	for (const TScriptInterface<IHUD_NotificationSource>& Source : Sources)
	{
		if (Source.GetObject() != nullptr && Source.GetInterface() != nullptr)
		{
			IHUD_NotificationSource::Execute_OnNotificationSinkUnbound(Source.GetObject());
		}
	}
	Sources.Reset();
	Items.Reset();
	NotificationMap = nullptr;
	ViewModel = nullptr;

	Super::Deinitialize();
}

UDP_MessageBusSubsystem* UHUD_NotificationSubsystem::GetBus() const
{
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(PC);
	}
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UGameInstance* GI = LP->GetGameInstance())
		{
			return GI->GetSubsystem<UDP_MessageBusSubsystem>();
		}
	}
	return nullptr;
}

APlayerController* UHUD_NotificationSubsystem::GetOwningPlayerController() const
{
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		return LP->GetPlayerController(LP->GetWorld());
	}
	return nullptr;
}

//================================ Bus subscriptions =================================

void UHUD_NotificationSubsystem::RefreshBusSubscriptions()
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (Bus == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[HUD] NotificationSubsystem: message bus unavailable; no subscriptions."));
		return;
	}

	// Drop every prior subscription owned by this subsystem, then re-add the current set. Cheap and
	// keeps us from leaking duplicate listeners across map swaps.
	Bus->StopListeningForOwner(this);

	// Direct request channels: producers broadcast FHUD_NotificationBusPayload / dismiss payloads.
	Bus->ListenNative(
		HUDTags::Bus_HUD_Notify,
		[this](const FDP_Message& Message) { HandleBusMessage(Message); },
		this, EDP_MessageMatch::ExactOrChild);

	Bus->ListenNative(
		HUDTags::Bus_HUD_Dismiss,
		[this](const FDP_Message& Message) { HandleBusMessage(Message); },
		this, EDP_MessageMatch::ExactOrChild);

	// Map-driven channels: subscribe once per distinct mapped channel.
	if (NotificationMap != nullptr)
	{
		TArray<FGameplayTag> Channels;
		NotificationMap->GetSubscribedChannels(Channels);
		for (const FGameplayTag& Channel : Channels)
		{
			Bus->ListenNative(
				Channel,
				[this](const FDP_Message& Message) { HandleBusMessage(Message); },
				this, EDP_MessageMatch::ExactOrChild);
		}
		UE_LOG(LogDP, Verbose, TEXT("[HUD] NotificationSubsystem subscribed to %d mapped channels."),
			Channels.Num());
	}
}

void UHUD_NotificationSubsystem::HandleBusMessage(const FDP_Message& Message)
{
	// 1) Direct notify request carrying a full notification payload.
	if (Message.Channel.MatchesTag(HUDTags::Bus_HUD_Notify))
	{
		if (const FHUD_NotificationBusPayload* Payload = Message.Payload.GetPtr<FHUD_NotificationBusPayload>())
		{
			PushNotification(Payload->Notification);
		}
		else
		{
			UE_LOG(LogDP, Verbose,
				TEXT("[HUD] DP.Bus.HUD.Notify message lacked an FHUD_NotificationBusPayload; ignoring."));
		}
		return;
	}

	// 2) Direct dismiss request.
	if (Message.Channel.MatchesTag(HUDTags::Bus_HUD_Dismiss))
	{
		if (const FHUD_NotificationDismissBusPayload* Payload =
				Message.Payload.GetPtr<FHUD_NotificationDismissBusPayload>())
		{
			DismissByKey(Payload->DedupeKey);
		}
		return;
	}

	// 3) Map-driven: resolve the best rule for this channel and surface its template.
	if (NotificationMap != nullptr)
	{
		if (const FHUD_NotificationMapEntry* Rule = NotificationMap->FindBestRule(Message.Channel))
		{
			PushNotification(Rule->Template);
		}
	}
}

//================================ Map / sources ====================================

void UHUD_NotificationSubsystem::SetNotificationMap(UHUD_NotificationMapDataAsset* InMap)
{
	NotificationMap = InMap;
	RefreshBusSubscriptions();
}

bool UHUD_NotificationSubsystem::SetNotificationMapByTag(FGameplayTag MapTag)
{
	if (!MapTag.IsValid())
	{
		return false;
	}

	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(GetOwningPlayerController());
	if (Registry == nullptr)
	{
		if (const ULocalPlayer* LP = GetLocalPlayer())
		{
			if (UGameInstance* GI = LP->GetGameInstance())
			{
				Registry = GI->GetSubsystem<UDP_DataRegistrySubsystem>();
			}
		}
	}
	if (Registry == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] SetNotificationMapByTag: data registry unavailable."));
		return false;
	}

	UHUD_NotificationMapDataAsset* Map = Registry->Find<UHUD_NotificationMapDataAsset>(MapTag);
	if (Map == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] SetNotificationMapByTag: no map registered under '%s'."),
			*MapTag.ToString());
		return false;
	}

	SetNotificationMap(Map);
	return true;
}

void UHUD_NotificationSubsystem::RegisterSource(const TScriptInterface<IHUD_NotificationSource>& Source)
{
	if (Source.GetObject() == nullptr || Source.GetInterface() == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] RegisterSource: null/invalid source."));
		return;
	}

	// Idempotent: do not double-register the same object.
	for (const TScriptInterface<IHUD_NotificationSource>& Existing : Sources)
	{
		if (Existing.GetObject() == Source.GetObject())
		{
			return;
		}
	}

	Sources.Add(Source);
	IHUD_NotificationSource::Execute_OnNotificationSinkBound(Source.GetObject(), this);

	// Drain anything the source buffered before it had a sink.
	TArray<FHUD_Notification> Pending;
	IHUD_NotificationSource::Execute_DrainPendingNotifications(Source.GetObject(), Pending);
	for (const FHUD_Notification& Notification : Pending)
	{
		PushNotification(Notification);
	}
}

void UHUD_NotificationSubsystem::UnregisterSource(const TScriptInterface<IHUD_NotificationSource>& Source)
{
	if (Source.GetObject() == nullptr)
	{
		return;
	}

	const int32 RemovedIndex = Sources.IndexOfByPredicate(
		[&Source](const TScriptInterface<IHUD_NotificationSource>& Existing)
		{
			return Existing.GetObject() == Source.GetObject();
		});

	if (RemovedIndex != INDEX_NONE)
	{
		if (Source.GetInterface() != nullptr)
		{
			IHUD_NotificationSource::Execute_OnNotificationSinkUnbound(Source.GetObject());
		}
		Sources.RemoveAt(RemovedIndex);
	}
}

//================================ Push / dismiss ===================================

void UHUD_NotificationSubsystem::NormalizeNotification(FHUD_Notification& InOut) const
{
	if (!InOut.Category.IsValid())
	{
		InOut.Category = HUDTags::Notify_Info;
	}
}

int64 UHUD_NotificationSubsystem::PushNotification(const FHUD_Notification& Notification)
{
	FHUD_Notification Normalized = Notification;
	NormalizeNotification(Normalized);

	if (!Normalized.HasContent())
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] PushNotification ignored: empty Title and Body."));
		return 0;
	}

	// De-dup: if an item with this (valid) key already exists, refresh it in place.
	if (Normalized.DedupeKey.IsValid())
	{
		if (FNotificationItem* Existing = FindByDedupeKey(Normalized.DedupeKey))
		{
			const int64 KeptId = Existing->InstanceId;
			Existing->Notification = Normalized;

			// Refresh the on-screen timer if the item is currently active.
			if (Existing->bActive)
			{
				Existing->TimeRemaining = Normalized.Duration;
			}

			// Priority may have changed: re-sort by extracting + re-inserting.
			FNotificationItem Moved = MoveTemp(*Existing);
			const int32 Index = IndexOfInstance(KeptId);
			if (Index != INDEX_NONE)
			{
				Items.RemoveAt(Index);
			}
			InsertByPriority(MoveTemp(Moved));

			PromoteQueued();
			PushToViewModel();
			return KeptId;
		}
	}

	FNotificationItem NewItem;
	NewItem.InstanceId = NextInstanceId++;
	NewItem.Notification = Normalized;
	NewItem.TimeRemaining = Normalized.Duration;
	NewItem.bActive = false;

	const int64 AssignedId = NewItem.InstanceId;
	InsertByPriority(MoveTemp(NewItem));

	PromoteQueued();
	PushToViewModel();
	return AssignedId;
}

int32 UHUD_NotificationSubsystem::DismissByKey(FGameplayTag Key)
{
	if (!Key.IsValid())
	{
		return 0;
	}

	const int32 Removed = Items.RemoveAll([&Key](const FNotificationItem& Item)
	{
		// Hierarchy-aware: a dismiss on a parent key removes child-keyed notifications.
		return Item.Notification.DedupeKey.IsValid() && Item.Notification.DedupeKey.MatchesTag(Key);
	});

	if (Removed > 0)
	{
		PromoteQueued();
		PushToViewModel();
	}
	return Removed;
}

bool UHUD_NotificationSubsystem::DismissByInstanceId(int64 InstanceId)
{
	const int32 Index = IndexOfInstance(InstanceId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Items.RemoveAt(Index);
	PromoteQueued();
	PushToViewModel();
	return true;
}

void UHUD_NotificationSubsystem::ClearAll()
{
	if (Items.Num() == 0)
	{
		return;
	}
	Items.Reset();
	PushToViewModel();
}

//================================ Queue mechanics ==================================

void UHUD_NotificationSubsystem::InsertByPriority(FNotificationItem&& Item)
{
	// Find the first existing item with strictly lower priority and insert before it; equal-priority
	// items keep insertion order (FIFO), so we skip past them.
	int32 InsertIndex = Items.Num();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i].Notification.Priority < Item.Notification.Priority)
		{
			InsertIndex = i;
			break;
		}
	}
	Items.Insert(MoveTemp(Item), InsertIndex);
}

UHUD_NotificationSubsystem::FNotificationItem* UHUD_NotificationSubsystem::FindByDedupeKey(const FGameplayTag& Key)
{
	if (!Key.IsValid())
	{
		return nullptr;
	}
	return Items.FindByPredicate([&Key](const FNotificationItem& Item)
	{
		return Item.Notification.DedupeKey == Key;
	});
}

int32 UHUD_NotificationSubsystem::IndexOfInstance(int64 InstanceId) const
{
	return Items.IndexOfByPredicate([InstanceId](const FNotificationItem& Item)
	{
		return Item.InstanceId == InstanceId;
	});
}

void UHUD_NotificationSubsystem::PromoteQueued()
{
	// Items is priority-sorted; the first MaxOnScreen are active, the rest queued. Newly-promoted
	// items (re)start their timer from the notification's duration.
	const int32 Cap = FMath::Max(1, MaxOnScreen);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const bool bShouldBeActive = (i < Cap);
		if (bShouldBeActive && !Items[i].bActive)
		{
			Items[i].bActive = true;
			Items[i].TimeRemaining = Items[i].Notification.Duration;
		}
		else if (!bShouldBeActive && Items[i].bActive)
		{
			// Demoted back to the queue (e.g. a higher-priority push arrived). Freeze its timer.
			Items[i].bActive = false;
		}
	}
}

bool UHUD_NotificationSubsystem::TickNotifications(float DeltaTime)
{
	bool bAnyExpired = false;

	for (int32 i = Items.Num() - 1; i >= 0; --i)
	{
		FNotificationItem& Item = Items[i];
		if (!Item.bActive)
		{
			continue;
		}

		// Duration <= 0 means sticky: never auto-expires.
		if (Item.Notification.Duration <= 0.f)
		{
			continue;
		}

		Item.TimeRemaining -= DeltaTime;
		if (Item.TimeRemaining <= 0.f)
		{
			Items.RemoveAt(i);
			bAnyExpired = true;
		}
	}

	if (bAnyExpired)
	{
		PromoteQueued();
		PushToViewModel();
	}
	else
	{
		// Even with nothing expiring, the visible items' TimeRemaining changed; refresh the view so
		// progress bars / countdowns update. Only push when something is actively counting down.
		bool bAnyCountingDown = false;
		for (const FNotificationItem& Item : Items)
		{
			if (Item.bActive && Item.Notification.Duration > 0.f)
			{
				bAnyCountingDown = true;
				break;
			}
		}
		if (bAnyCountingDown)
		{
			PushToViewModel();
		}
	}

	// Keep ticking for the life of the subsystem.
	return true;
}

void UHUD_NotificationSubsystem::PushToViewModel()
{
	if (ViewModel == nullptr)
	{
		return;
	}

	TArray<FHUD_ActiveNotificationView> Visible;
	int32 QueuedCount = 0;

	for (const FNotificationItem& Item : Items)
	{
		if (Item.bActive)
		{
			FHUD_ActiveNotificationView& View = Visible.AddDefaulted_GetRef();
			View.InstanceId = Item.InstanceId;
			View.Notification = Item.Notification;
			View.TimeRemaining = Item.TimeRemaining;
		}
		else
		{
			++QueuedCount;
		}
	}

	ViewModel->SetVisibleNotifications(Visible);
	ViewModel->SetQueuedCount(QueuedCount);
}

//================================ Config / debug ===================================

void UHUD_NotificationSubsystem::SetMaxOnScreen(int32 InMaxOnScreen)
{
	const int32 Clamped = FMath::Max(1, InMaxOnScreen);
	if (Clamped == MaxOnScreen)
	{
		return;
	}
	MaxOnScreen = Clamped;
	PromoteQueued();
	PushToViewModel();
}

void UHUD_NotificationSubsystem::DumpTo(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("HUD Notifications: %d items (cap %d), map=%s"),
		Items.Num(), MaxOnScreen,
		NotificationMap ? *NotificationMap->GetName() : TEXT("<none>")));

	for (const FNotificationItem& Item : Items)
	{
		OutLines.Add(FString::Printf(
			TEXT("  [%s] id=%lld prio=%d key='%s' cat='%s' title='%s' rem=%.1f"),
			Item.bActive ? TEXT("ACTIVE") : TEXT("queued"),
			Item.InstanceId,
			Item.Notification.Priority,
			*Item.Notification.DedupeKey.ToString(),
			*Item.Notification.Category.ToString(),
			*Item.Notification.Title.ToString(),
			Item.TimeRemaining));
	}
}
