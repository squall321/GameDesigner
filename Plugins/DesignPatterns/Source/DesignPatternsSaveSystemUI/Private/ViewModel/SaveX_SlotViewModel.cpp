// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/SaveX_SlotViewModel.h"

#include "SaveX_ServiceKeys.h"                 // SaveX_ServiceKeys::SlotManager() conventional key
#include "Settings/SaveX_DeveloperSettings.h"  // configured SlotManagerServiceTag override

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"           // FDP_SubsystemStatics::GetGameInstanceSubsystem<T>
#include "Services/DPServiceLocatorSubsystem.h"

#include "Persist/Seam_SaveSlotManager.h"

#include "Internationalization/Text.h"

#define LOCTEXT_NAMESPACE "SaveXSlotViewModel"

// Register the FieldNotify field ids declared in the header. These must match the FieldNotify-tagged getters.
UE_FIELD_NOTIFICATION_IMPLEMENTATION_BEGIN(USaveX_SlotViewModel)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(USaveX_SlotViewModel, Slots)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(USaveX_SlotViewModel, HasAnySlots)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(USaveX_SlotViewModel, SlotCount)
	UE_FIELD_NOTIFICATION_IMPLEMENT_FIELD(USaveX_SlotViewModel, MostRecentSlotName)
UE_FIELD_NOTIFICATION_IMPLEMENTATION_END(USaveX_SlotViewModel)

USaveX_SlotViewModel::USaveX_SlotViewModel()
{
	// Pure data ViewModel: no ticking, no world refs, no replicated state. Rows are populated on demand via
	// Refresh() by the owning widget/mediator.
}

void USaveX_SlotViewModel::Refresh()
{
	// Re-resolve the seam every Refresh; never hold a hard reference across frames so a torn-down backend
	// cannot dangle. An unset seam is the documented inert default: an empty, logged list.
	TScriptInterface<ISeam_SaveSlotManager> SlotManager = ResolveSlotManager();
	if (!SlotManager)
	{
		UE_LOG(LogDPSave, Verbose,
			TEXT("[SaveUI] Refresh: no slot-manager backend registered; presenting an empty slot list."));
		Clear();
		// Still notify a one-shot rebuild so a view that opened before the backend existed clears itself.
		OnSlotsRefreshed.Broadcast();
		return;
	}

	UObject* SeamObject = SlotManager.GetObject();

	// Pull the most-recent slot first so each projected row can carry its "is most recent" badge in one pass.
	const FString NewMostRecent = ISeam_SaveSlotManager::Execute_GetMostRecentSlot(SeamObject);

	TArray<FSeam_SaveSlotInfo> RawSlots;
	ISeam_SaveSlotManager::Execute_GetAllSlots(SeamObject, RawSlots);

	// Project + stable sort: existing slots first, then most-recent-first by timestamp, then by name so the
	// list ordering is deterministic for the UI regardless of the backend's enumeration order.
	TArray<FSaveX_SlotRow> NewRows;
	NewRows.Reserve(RawSlots.Num());
	for (const FSeam_SaveSlotInfo& Info : RawSlots)
	{
		NewRows.Add(ProjectRow(Info, NewMostRecent));
	}
	NewRows.Sort([](const FSaveX_SlotRow& A, const FSaveX_SlotRow& B)
	{
		if (A.bExists != B.bExists)
		{
			return A.bExists; // existing slots sort before non-existent placeholders
		}
		if (A.Timestamp != B.Timestamp)
		{
			return A.Timestamp > B.Timestamp; // newest first
		}
		return A.SlotName < B.SlotName; // stable tiebreak
	});

	// Store-and-notify each observable field only when it actually changed, so bound views re-read minimally.
	const bool bSlotsChanged = (NewRows != Slots);
	if (bSlotsChanged)
	{
		const int32 OldCount = Slots.Num();
		Slots = MoveTemp(NewRows);
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Slots);
		if (Slots.Num() != OldCount)
		{
			BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::SlotCount);
		}
	}

	const bool bNewHasAny = (Slots.Num() > 0);
	if (bNewHasAny != bHasAnySlots)
	{
		bHasAnySlots = bNewHasAny;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::HasAnySlots);
	}

	if (NewMostRecent != MostRecentSlotName)
	{
		MostRecentSlotName = NewMostRecent;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::MostRecentSlotName);
	}

	UE_LOG(LogDPSave, Verbose, TEXT("[SaveUI] Refresh: %d slot row(s) (mostRecent='%s')."),
		Slots.Num(), *MostRecentSlotName);

	// Single coarse "rebuild your list" signal for views that prefer it over per-field binding.
	OnSlotsRefreshed.Broadcast();
}

void USaveX_SlotViewModel::Clear()
{
	const bool bHadRows = Slots.Num() > 0;

	if (bHadRows)
	{
		Slots.Reset();
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Slots);
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::SlotCount);
	}
	if (bHasAnySlots)
	{
		bHasAnySlots = false;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::HasAnySlots);
	}
	if (!MostRecentSlotName.IsEmpty())
	{
		MostRecentSlotName.Reset();
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::MostRecentSlotName);
	}
}

void USaveX_SlotViewModel::SetServiceKeyOverride(FGameplayTag InServiceKey)
{
	// An invalid tag restores default resolution (settings/conventional key) on the next Refresh.
	ServiceKeyOverride = InServiceKey;
}

bool USaveX_SlotViewModel::FindSlotRow(const FString& SlotName, FSaveX_SlotRow& OutRow) const
{
	for (const FSaveX_SlotRow& Row : Slots)
	{
		if (Row.SlotName == SlotName)
		{
			OutRow = Row;
			return true;
		}
	}
	return false;
}

bool USaveX_SlotViewModel::DoesSlotExist(const FString& SlotName) const
{
	if (SlotName.IsEmpty())
	{
		return false;
	}
	TScriptInterface<ISeam_SaveSlotManager> SlotManager = ResolveSlotManager();
	if (!SlotManager)
	{
		return false;
	}
	return ISeam_SaveSlotManager::Execute_DoesSlotExist(SlotManager.GetObject(), SlotName);
}

TScriptInterface<ISeam_SaveSlotManager> USaveX_SlotViewModel::ResolveSlotManager() const
{
	TScriptInterface<ISeam_SaveSlotManager> Result;

	// GI-scoped locator; resolved via the world context this ViewModel lives under.
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Result;
	}

	const FGameplayTag Key = ResolveServiceKey();
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

FGameplayTag USaveX_SlotViewModel::ResolveServiceKey() const
{
	// Precedence: explicit override > project-configured settings tag > conventional SaveSystem key.
	if (ServiceKeyOverride.IsValid())
	{
		return ServiceKeyOverride;
	}
	if (const USaveX_DeveloperSettings* Settings = USaveX_DeveloperSettings::Get())
	{
		if (Settings->SlotManagerServiceTag.IsValid())
		{
			return Settings->SlotManagerServiceTag;
		}
	}
	return SaveX_ServiceKeys::SlotManager();
}

FSaveX_SlotRow USaveX_SlotViewModel::ProjectRow(const FSeam_SaveSlotInfo& Info, const FString& MostRecent)
{
	FSaveX_SlotRow Row;
	Row.SlotName = Info.SlotName;
	Row.DisplayName = Info.DisplayName.IsEmpty() ? FText::FromString(Info.SlotName) : Info.DisplayName;
	Row.Timestamp = Info.Timestamp;
	Row.TimestampText = FormatTimestamp(Info.Timestamp);
	Row.PlaytimeSeconds = Info.PlaytimeSeconds;
	Row.PlaytimeText = FormatPlaytime(Info.PlaytimeSeconds);
	Row.bExists = Info.bExists;
	Row.bIsMostRecent = !MostRecent.IsEmpty() && (Info.SlotName == MostRecent);
	return Row;
}

FText USaveX_SlotViewModel::FormatPlaytime(float Seconds)
{
	// Defensive clamp: negative/NaN playtime collapses to zero rather than producing a garbage label.
	if (!FMath::IsFinite(Seconds) || Seconds < 0.f)
	{
		Seconds = 0.f;
	}

	const int32 TotalSeconds = FMath::FloorToInt(Seconds);
	const int32 Hours = TotalSeconds / 3600;
	const int32 Minutes = (TotalSeconds % 3600) / 60;
	const int32 Secs = TotalSeconds % 60;

	FNumberFormattingOptions TwoDigits;
	TwoDigits.SetMinimumIntegralDigits(2);

	if (Hours > 0)
	{
		// "Xh YYm"
		return FText::Format(
			LOCTEXT("PlaytimeHM", "{0}h {1}m"),
			FText::AsNumber(Hours),
			FText::AsNumber(Minutes, &TwoDigits));
	}
	// "Mm SSs"
	return FText::Format(
		LOCTEXT("PlaytimeMS", "{0}m {1}s"),
		FText::AsNumber(Minutes),
		FText::AsNumber(Secs, &TwoDigits));
}

FText USaveX_SlotViewModel::FormatTimestamp(const FDateTime& Timestamp)
{
	// FDateTime(0) is the seam's "unknown" sentinel; present nothing rather than the epoch.
	if (Timestamp.GetTicks() == 0)
	{
		return FText::GetEmpty();
	}
	// Locale-aware short date + time. FText::AsDateTime localizes per the running culture.
	return FText::AsDateTime(Timestamp, EDateTimeStyle::Short, EDateTimeStyle::Short, FText::GetInvariantTimeZone());
}

#undef LOCTEXT_NAMESPACE
