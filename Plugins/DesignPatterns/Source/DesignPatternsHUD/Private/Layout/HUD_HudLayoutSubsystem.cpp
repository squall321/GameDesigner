// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Layout/HUD_HudLayoutSubsystem.h"
#include "Data/HUD_HudLayoutDataAsset.h"
#include "Notification/HUD_NotificationTypes.h"
#include "HUD_HudNotifyTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"

void UHUD_HudLayoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to the specific HUD layout-control channels so already-replicated gameplay can drive
	// layout rebuilds and per-slot visibility without coupling producers to this subsystem. We listen
	// on each leaf channel (rather than a shared root) so the handler only runs for messages it acts on.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		auto Subscribe = [this, Bus](const FGameplayTag& Channel)
		{
			Bus->ListenNative(
				Channel,
				[this](const FDP_Message& Message) { HandleHudBusMessage(Message); },
				this,
				EDP_MessageMatch::ExactOrChild);
		};
		Subscribe(HUDTags::Bus_HUD_LayoutRebuild);
		Subscribe(HUDTags::Bus_HUD_SlotShow);
		Subscribe(HUDTags::Bus_HUD_SlotHide);
	}

	UE_LOG(LogDP, Verbose, TEXT("[HUD] LayoutSubsystem initialized for local player."));
}

void UHUD_HudLayoutSubsystem::Deinitialize()
{
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}

	ClearLayout();
	ActiveLayout = nullptr;

	// Cancel any still-pending widget-class streams.
	for (TPair<FGameplayTag, TSharedPtr<FStreamableHandle>>& Pair : ActiveStreams)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->CancelHandle();
		}
	}
	ActiveStreams.Reset();

	Super::Deinitialize();
}

UDP_MessageBusSubsystem* UHUD_HudLayoutSubsystem::GetBus() const
{
	// Local player subsystems have a GetLocalPlayer(); the bus is game-instance scoped. Resolve via
	// the player controller (a valid world-context object) so every null hop is guarded.
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

APlayerController* UHUD_HudLayoutSubsystem::GetOwningPlayerController() const
{
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		return LP->GetPlayerController(LP->GetWorld());
	}
	return nullptr;
}

void UHUD_HudLayoutSubsystem::HandleHudBusMessage(const FDP_Message& Message)
{
	// Re-apply layout on an explicit rebuild request.
	if (Message.Channel.MatchesTag(HUDTags::Bus_HUD_LayoutRebuild))
	{
		RebuildLayout();
		return;
	}

	// Slot show/hide: producers broadcast on DP.Bus.HUD.SlotShow / .SlotHide with an
	// FHUD_SlotVisibilityBusPayload naming the slot. If no valid payload is present we log and ignore
	// rather than guessing, keeping producers decoupled from this subsystem.
	if (Message.Channel.MatchesTag(HUDTags::Bus_HUD_SlotShow) ||
		Message.Channel.MatchesTag(HUDTags::Bus_HUD_SlotHide))
	{
		FGameplayTag SlotTag;
		if (const FHUD_SlotVisibilityBusPayload* Payload =
				Message.Payload.GetPtr<FHUD_SlotVisibilityBusPayload>())
		{
			SlotTag = Payload->SlotTag;
		}

		if (!SlotTag.IsValid())
		{
			UE_LOG(LogDP, Verbose,
				TEXT("[HUD] SlotShow/Hide bus message lacked a valid FHUD_SlotVisibilityBusPayload; ignoring."));
			return;
		}

		if (Message.Channel.MatchesTag(HUDTags::Bus_HUD_SlotShow))
		{
			ShowSlot(SlotTag);
		}
		else
		{
			HideSlot(SlotTag);
		}
	}
}

void UHUD_HudLayoutSubsystem::ApplyLayout(UHUD_HudLayoutDataAsset* Layout)
{
	// Tear down the old layout completely before swapping the asset.
	ClearLayout();
	ActiveLayout = Layout;

	if (ActiveLayout == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[HUD] ApplyLayout(null): HUD cleared."));
		return;
	}

	for (const FHUD_LayoutSlot& SlotDef : ActiveLayout->Slots)
	{
		if (!SlotDef.IsValidSlot())
		{
			UE_LOG(LogDP, Warning,
				TEXT("[HUD] Layout '%s' contains an invalid slot (tag '%s'); skipped."),
				*ActiveLayout->GetName(), *SlotDef.SlotTag.ToString());
			continue;
		}

		FHUD_LiveSlot& LiveSlot = LiveSlots.Add(SlotDef.SlotTag);
		LiveSlot.SlotTag = SlotDef.SlotTag;
		LiveSlot.LayerTag = SlotDef.LayerTag.IsValid() ? SlotDef.LayerTag : ActiveLayout->DefaultLayer;
		LiveSlot.ZOrder = SlotDef.ZOrder;
		LiveSlot.bVisible = SlotDef.bVisibleByDefault;
		LiveSlot.bLoading = false;

		// Only stream + create widgets for slots that should be visible up front; hidden slots are
		// realised lazily on the first ShowSlot, avoiding loading widgets the player never sees.
		if (LiveSlot.bVisible)
		{
			StreamAndCreateSlotWidget(SlotDef.SlotTag);
		}
	}

	UE_LOG(LogDP, Log, TEXT("[HUD] Applied layout '%s' (%d slots)."),
		*ActiveLayout->GetName(), ActiveLayout->Slots.Num());
}

bool UHUD_HudLayoutSubsystem::ApplyLayoutByTag(FGameplayTag LayoutTag)
{
	if (!LayoutTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] ApplyLayoutByTag called with an invalid tag."));
		return false;
	}

	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(GetOwningPlayerController());
	if (Registry == nullptr)
	{
		// Fall back to the local player's game instance if we have no controller yet.
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
		UE_LOG(LogDP, Warning, TEXT("[HUD] ApplyLayoutByTag: data registry unavailable."));
		return false;
	}

	UHUD_HudLayoutDataAsset* Layout = Registry->Find<UHUD_HudLayoutDataAsset>(LayoutTag);
	if (Layout == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] ApplyLayoutByTag: no HUD layout registered under '%s'."),
			*LayoutTag.ToString());
		return false;
	}

	ApplyLayout(Layout);
	return true;
}

void UHUD_HudLayoutSubsystem::ClearLayout()
{
	for (TPair<FGameplayTag, FHUD_LiveSlot>& Pair : LiveSlots)
	{
		TeardownLiveSlot(Pair.Value);
	}
	LiveSlots.Reset();
}

void UHUD_HudLayoutSubsystem::RebuildLayout()
{
	if (ActiveLayout == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[HUD] RebuildLayout: no active layout."));
		return;
	}

	// Re-apply the same asset (ApplyLayout clears first, so this is a clean rebuild).
	UHUD_HudLayoutDataAsset* Layout = ActiveLayout;
	ApplyLayout(Layout);
}

bool UHUD_HudLayoutSubsystem::ShowSlot(FGameplayTag SlotTag)
{
	FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	if (LiveSlot == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] ShowSlot: slot '%s' is not in the active layout."),
			*SlotTag.ToString());
		return false;
	}

	LiveSlot->bVisible = true;

	if (LiveSlot->Widget != nullptr)
	{
		// Already realised: just flip visibility.
		ApplyWidgetVisibility(*LiveSlot);
	}
	else if (!LiveSlot->bLoading)
	{
		// Lazily stream + create on first show.
		StreamAndCreateSlotWidget(SlotTag);
	}
	return true;
}

bool UHUD_HudLayoutSubsystem::HideSlot(FGameplayTag SlotTag)
{
	FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	if (LiveSlot == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] HideSlot: slot '%s' is not in the active layout."),
			*SlotTag.ToString());
		return false;
	}

	LiveSlot->bVisible = false;
	if (LiveSlot->Widget != nullptr)
	{
		ApplyWidgetVisibility(*LiveSlot);
	}
	return true;
}

bool UHUD_HudLayoutSubsystem::IsSlotVisible(FGameplayTag SlotTag) const
{
	const FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	return LiveSlot != nullptr && LiveSlot->bVisible && LiveSlot->Widget != nullptr;
}

UUserWidget* UHUD_HudLayoutSubsystem::GetSlotWidget(FGameplayTag SlotTag) const
{
	const FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	return LiveSlot ? LiveSlot->Widget : nullptr;
}

void UHUD_HudLayoutSubsystem::StreamAndCreateSlotWidget(const FGameplayTag& SlotTag)
{
	if (ActiveLayout == nullptr)
	{
		return;
	}

	const FHUD_LayoutSlot* SlotDef = ActiveLayout->FindSlot(SlotTag);
	FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	if (SlotDef == nullptr || LiveSlot == nullptr)
	{
		return;
	}

	// Fast path: class already loaded in memory — create synchronously.
	if (UClass* AlreadyLoaded = SlotDef->WidgetClass.Get())
	{
		LiveSlot->bLoading = false;
		CreateAndAddSlotWidget(*SlotDef, *LiveSlot, AlreadyLoaded);
		return;
	}

	const FSoftObjectPath ClassPath = SlotDef->WidgetClass.ToSoftObjectPath();
	if (!ClassPath.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] Slot '%s' has an unset widget class path."),
			*SlotTag.ToString());
		return;
	}

	// Cancel any prior in-flight stream for this slot (re-apply / rapid re-show).
	if (TSharedPtr<FStreamableHandle>* Existing = ActiveStreams.Find(SlotTag))
	{
		if (Existing->IsValid())
		{
			(*Existing)->CancelHandle();
		}
		ActiveStreams.Remove(SlotTag);
	}

	LiveSlot->bLoading = true;

	TWeakObjectPtr<UHUD_HudLayoutSubsystem> WeakThis(this);
	const FGameplayTag CapturedTag = SlotTag;
	TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
		ClassPath,
		FStreamableDelegate::CreateLambda([WeakThis, CapturedTag]()
		{
			if (UHUD_HudLayoutSubsystem* Self = WeakThis.Get())
			{
				Self->OnSlotClassLoaded(CapturedTag);
			}
		}),
		FStreamableManager::DefaultAsyncLoadPriority);

	if (Handle.IsValid())
	{
		ActiveStreams.Add(SlotTag, Handle);
	}
	else
	{
		// RequestAsyncLoad can return null if the asset is already loaded; resolve immediately.
		LiveSlot->bLoading = false;
		OnSlotClassLoaded(SlotTag);
	}
}

void UHUD_HudLayoutSubsystem::OnSlotClassLoaded(FGameplayTag SlotTag)
{
	ActiveStreams.Remove(SlotTag);

	if (ActiveLayout == nullptr)
	{
		return;
	}

	const FHUD_LayoutSlot* SlotDef = ActiveLayout->FindSlot(SlotTag);
	FHUD_LiveSlot* LiveSlot = LiveSlots.Find(SlotTag);
	if (SlotDef == nullptr || LiveSlot == nullptr)
	{
		// The slot was removed by a layout swap while loading — nothing to do.
		return;
	}

	LiveSlot->bLoading = false;

	UClass* LoadedClass = SlotDef->WidgetClass.Get();
	if (LoadedClass == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] Slot '%s' widget class failed to load."),
			*SlotTag.ToString());
		return;
	}

	CreateAndAddSlotWidget(*SlotDef, *LiveSlot, LoadedClass);
}

void UHUD_HudLayoutSubsystem::CreateAndAddSlotWidget(const FHUD_LayoutSlot& SlotDef,
	FHUD_LiveSlot& LiveSlot, UClass* LoadedClass)
{
	APlayerController* OwningPC = GetOwningPlayerController();
	if (OwningPC == nullptr)
	{
		UE_LOG(LogDP, Warning,
			TEXT("[HUD] Cannot create slot '%s' widget: no owning player controller yet."),
			*SlotDef.SlotTag.ToString());
		return;
	}

	if (LiveSlot.Widget != nullptr)
	{
		// Already realised (e.g. a duplicate completion) — just refresh visibility.
		ApplyWidgetVisibility(LiveSlot);
		return;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(OwningPC, LoadedClass);
	if (Widget == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[HUD] CreateWidget failed for slot '%s'."),
			*SlotDef.SlotTag.ToString());
		return;
	}

	LiveSlot.Widget = Widget;
	Widget->AddToPlayerScreen(LiveSlot.ZOrder);
	ApplyWidgetVisibility(LiveSlot);

	UE_LOG(LogDP, Verbose, TEXT("[HUD] Realised slot '%s' (layer '%s', z=%d, visible=%d)."),
		*SlotDef.SlotTag.ToString(), *LiveSlot.LayerTag.ToString(), LiveSlot.ZOrder,
		LiveSlot.bVisible ? 1 : 0);
}

void UHUD_HudLayoutSubsystem::ApplyWidgetVisibility(FHUD_LiveSlot& LiveSlot)
{
	if (LiveSlot.Widget == nullptr)
	{
		return;
	}
	LiveSlot.Widget->SetVisibility(LiveSlot.bVisible
		? ESlateVisibility::SelfHitTestInvisible
		: ESlateVisibility::Collapsed);
}

void UHUD_HudLayoutSubsystem::TeardownLiveSlot(FHUD_LiveSlot& LiveSlot)
{
	// Cancel any in-flight stream for this slot.
	if (TSharedPtr<FStreamableHandle>* Existing = ActiveStreams.Find(LiveSlot.SlotTag))
	{
		if (Existing->IsValid())
		{
			(*Existing)->CancelHandle();
		}
		ActiveStreams.Remove(LiveSlot.SlotTag);
	}

	if (LiveSlot.Widget != nullptr)
	{
		LiveSlot.Widget->RemoveFromParent();
		LiveSlot.Widget = nullptr;
	}
	LiveSlot.bLoading = false;
}

void UHUD_HudLayoutSubsystem::DumpTo(TArray<FString>& OutLines) const
{
	OutLines.Add(FString::Printf(TEXT("HUD Layout: %s (%d live slots)"),
		ActiveLayout ? *ActiveLayout->GetName() : TEXT("<none>"), LiveSlots.Num()));

	for (const TPair<FGameplayTag, FHUD_LiveSlot>& Pair : LiveSlots)
	{
		const FHUD_LiveSlot& Slot = Pair.Value;
		OutLines.Add(FString::Printf(
			TEXT("  Slot '%s' layer='%s' z=%d visible=%d widget=%s%s"),
			*Slot.SlotTag.ToString(), *Slot.LayerTag.ToString(), Slot.ZOrder,
			Slot.bVisible ? 1 : 0,
			Slot.Widget ? *Slot.Widget->GetName() : TEXT("<null>"),
			Slot.bLoading ? TEXT(" (loading)") : TEXT("")));
	}
}
