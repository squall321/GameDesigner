// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subtitle/Loc_RichSubtitleSubsystem.h"

#include "Subtitle/Loc_SpeakerStyleDataAsset.h"
#include "Subtitle/Loc_SubtitleHistoryViewModel.h"
#include "Localization/Loc_LocalizationSubsystem.h"
#include "Accessibility/Loc_AccessibilityLibrary.h"
#include "Accessibility/Loc_AccessibilitySubsystem.h"
#include "Settings/Loc_DeveloperSettings.h"
#include "DesignPatternsLocalizationModule.h"

#include "Core/DPLog.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{
	constexpr int32 GFallbackHistoryCap = 64;
}

void ULoc_RichSubtitleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	HistoryViewModel = NewObject<ULoc_SubtitleHistoryViewModel>(this, TEXT("Loc_SubtitleHistoryViewModel"));

	// Resolve the speaker-style asset from settings (DataTag) via the data registry, if configured.
	if (const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get())
	{
		if (Settings->SpeakerStyleDataTag.IsValid())
		{
			if (UDP_DataRegistrySubsystem* Registry = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_DataRegistrySubsystem>() : nullptr)
			{
				SpeakerStyles = Registry->Find<ULoc_SpeakerStyleDataAsset>(Settings->SpeakerStyleDataTag);
			}
		}
	}

	SubscribeBus();
	RegisterWithAccessibilityProvider();

	// Seed the VM with an empty backlog so the UI binds to sane values.
	if (HistoryViewModel)
	{
		HistoryViewModel->SetHistory(History, UnreadCount);
	}

	UE_LOG(LogDP, Log, TEXT("ULoc_RichSubtitleSubsystem initialized (styles=%s)."),
		SpeakerStyles ? TEXT("loaded") : TEXT("none"));
}

void ULoc_RichSubtitleSubsystem::Deinitialize()
{
	UnregisterFromAccessibilityProvider();

	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}
	ListenerHandles.Reset();

	History.Reset();
	HistoryViewModel = nullptr;
	SpeakerStyles = nullptr;

	Super::Deinitialize();
}

UDP_MessageBusSubsystem* ULoc_RichSubtitleSubsystem::GetBus() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr;
}

void ULoc_RichSubtitleSubsystem::SubscribeBus()
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_RichSubtitleSubsystem: no message bus; backlog will not auto-populate."));
		return;
	}

	auto LineHandler = [this](const FDP_Message& Msg) { HandleLineMessage(Msg); };

	// Subscribe to the FLoc_SubtitleLine-carrying channels ONLY — NOT Bus_SubtitleChanged (visible-set signal).
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_DialogueLine, LineHandler, this));
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_VoiceLine,    LineHandler, this));
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_SubtitleShow, LineHandler, this));
}

void ULoc_RichSubtitleSubsystem::HandleLineMessage(const FDP_Message& Message)
{
	const FLoc_SubtitleLine* LinePtr = Message.Payload.GetPtr<FLoc_SubtitleLine>();
	if (!LinePtr)
	{
		return; // wrong payload type; the base subtitle subsystem logs verbosely, no need to double-log.
	}

	FLoc_SubtitleHistoryEntry Entry;
	Entry.Line = *LinePtr;
	Entry.Style = ResolveStyle(*LinePtr);

	// Timestamp from the world clock if reachable, else 0 (history ordering is by insertion regardless).
	double Now = 0.0;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			Now = World->GetTimeSeconds();
		}
	}
	Entry.TimestampSeconds = Now;

	AppendHistory(Entry);
}

FLoc_ResolvedSubtitleStyle ULoc_RichSubtitleSubsystem::ResolveStyle(const FLoc_SubtitleLine& Line) const
{
	FLoc_ResolvedSubtitleStyle Resolved;

	// Pull the speaker style (or default) from the asset.
	FLoc_SpeakerStyle Style;
	if (SpeakerStyles)
	{
		SpeakerStyles->FindStyle(Line.Speaker, Style);
	}

	// Resolve the speaker display name via the localization subsystem (if a key is set).
	if (Style.DisplayNameKey.IsValid())
	{
		if (ULoc_LocalizationSubsystem* Loc = GetGameInstance() ? GetGameInstance()->GetSubsystem<ULoc_LocalizationSubsystem>() : nullptr)
		{
			bool bFound = false;
			Resolved.SpeakerName = Loc->FindText(Style.DisplayNameKey, bFound);
		}
	}

	// Apply colorblind correction to the name color using the canonical library remap.
	Resolved.NameColor = ULoc_AccessibilityLibrary::ApplyColorblindToColor(Style.NameColor, CurrentOptions.ColorblindMode);

	Resolved.Anchor = Style.Anchor;
	Resolved.FontRole = Style.FontRole.IsValid() ? Style.FontRole : DPLocTags::Font_Subtitle;
	Resolved.Portrait = Style.Portrait;
	Resolved.bDrawBackground = CurrentOptions.bSubtitleBackground;

	return Resolved;
}

void ULoc_RichSubtitleSubsystem::AppendHistory(const FLoc_SubtitleHistoryEntry& Entry)
{
	History.Add(Entry);

	const int32 Cap = ResolveHistoryCap();
	while (History.Num() > Cap)
	{
		History.RemoveAt(0);
	}

	++UnreadCount;
	// Unread can never exceed the number retained.
	UnreadCount = FMath::Min(UnreadCount, History.Num());

	if (HistoryViewModel)
	{
		HistoryViewModel->SetHistory(History, UnreadCount);
	}
}

TArray<FLoc_SubtitleHistoryEntry> ULoc_RichSubtitleSubsystem::GetHistory(int32 MaxEntries) const
{
	if (MaxEntries <= 0 || MaxEntries >= History.Num())
	{
		return History;
	}

	TArray<FLoc_SubtitleHistoryEntry> Out;
	Out.Reserve(MaxEntries);
	const int32 Start = History.Num() - MaxEntries;
	for (int32 i = Start; i < History.Num(); ++i)
	{
		Out.Add(History[i]);
	}
	return Out;
}

void ULoc_RichSubtitleSubsystem::ClearHistory()
{
	History.Reset();
	UnreadCount = 0;
	if (HistoryViewModel)
	{
		HistoryViewModel->SetHistory(History, UnreadCount);
	}
}

void ULoc_RichSubtitleSubsystem::SetSpeakerStyleAsset(ULoc_SpeakerStyleDataAsset* InAsset)
{
	SpeakerStyles = InAsset;
}

void ULoc_RichSubtitleSubsystem::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	CurrentOptions = Options;
	// Existing backlog entries keep their original styling (they were resolved when shown); only new lines
	// pick up the new options. The VM is not re-pushed here — past entries are immutable history.
}

void ULoc_RichSubtitleSubsystem::RegisterWithAccessibilityProvider()
{
	if (bRegisteredAccessibility)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return;
	}

	// The accessibility provider publishes a RegisterConsumer-capable subsystem under this key. We resolve
	// it generically and register ourselves as an ISeam_AccessibilityConsumer so options are pushed to us.
	UObject* Provider = Locator->ResolveService(DPLocTags::Service_AccessibilityProvider);
	if (!Provider)
	{
		// Inert default: no provider -> we keep the all-on CurrentOptions defaults; styling still works.
		return;
	}

	// The shipped provider is ULoc_AccessibilitySubsystem; call its RegisterConsumer via the concrete type
	// when present. We avoid a hard include cycle by resolving through the GameInstance subsystem directly.
	if (ULoc_AccessibilitySubsystem* Access = GI->GetSubsystem<ULoc_AccessibilitySubsystem>())
	{
		TScriptInterface<ISeam_AccessibilityConsumer> Self;
		Self.SetObject(this);
		Self.SetInterface(Cast<ISeam_AccessibilityConsumer>(this));
		Access->RegisterConsumer(Self);
		bRegisteredAccessibility = true;
	}
}

void ULoc_RichSubtitleSubsystem::UnregisterFromAccessibilityProvider()
{
	if (!bRegisteredAccessibility)
	{
		return;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (ULoc_AccessibilitySubsystem* Access = GI->GetSubsystem<ULoc_AccessibilitySubsystem>())
		{
			TScriptInterface<ISeam_AccessibilityConsumer> Self;
			Self.SetObject(this);
			Self.SetInterface(Cast<ISeam_AccessibilityConsumer>(this));
			Access->UnregisterConsumer(Self);
		}
	}
	bRegisteredAccessibility = false;
}

int32 ULoc_RichSubtitleSubsystem::ResolveHistoryCap() const
{
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const int32 Cap = Settings ? Settings->SubtitleHistoryCap : GFallbackHistoryCap;
	return FMath::Max(1, Cap);
}

FString ULoc_RichSubtitleSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("RichSubtitles: history=%d unread=%d styles=%s"),
		History.Num(), UnreadCount, SpeakerStyles ? TEXT("yes") : TEXT("no"));
}
