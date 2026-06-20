// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subtitle/Loc_SubtitleSubsystem.h"

#include "DesignPatternsLocalizationModule.h"
#include "Settings/Loc_DeveloperSettings.h"
#include "Subtitle/Loc_SubtitleViewModel.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Loc/Seam_TextToSpeech.h"

// FInstancedStruct: StructUtils on 5.3/5.4, CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Defensive fallbacks used only when the settings CDO is unreadable. Mirrors the CDO inline defaults. */
	constexpr float GFallbackSecondsPerChar = 0.06f;
	constexpr float GFallbackMinDuration    = 1.5f;
	constexpr float GFallbackMaxDuration    = 12.0f;
	constexpr int32 GFallbackMaxOnScreen    = 2;
}

void ULoc_SubtitleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ViewModel = NewObject<ULoc_SubtitleViewModel>(this, TEXT("Loc_SubtitleViewModel"));

	// Seed the ViewModel with the inert all-on accessibility defaults so the UI binds to sane values
	// even before any accessibility provider pushes options.
	PushAccessibilityToViewModel();
	PushVisibleToViewModel();

	SubscribeBus();

	// Drain subtitle timers once per frame via the global ticker (the subsystem is NOT an FTickableGameObject).
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULoc_SubtitleSubsystem::TickSubtitles));

	UE_LOG(LogDP, Log, TEXT("ULoc_SubtitleSubsystem initialized."));
}

void ULoc_SubtitleSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Stop listening cleanly: remove every listener we own, by owner, so nothing fires post-teardown.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}
	ListenerHandles.Reset();

	Items.Reset();
	CachedTTS.Reset();
	ViewModel = nullptr;

	Super::Deinitialize();
}

void ULoc_SubtitleSubsystem::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	const bool bWasEnabled = CurrentOptions.bSubtitlesEnabled;
	CurrentOptions = Options;

	PushAccessibilityToViewModel();

	// If subtitles were just turned off, clear everything on screen.
	if (bWasEnabled && !CurrentOptions.bSubtitlesEnabled)
	{
		ClearSubtitles();
	}
	else
	{
		// Presentation flags may have changed even if enabled-state did not; refresh the visible push.
		PushVisibleToViewModel();
	}
}

int64 ULoc_SubtitleSubsystem::ShowSubtitle(const FLoc_SubtitleLine& Line)
{
	if (!CurrentOptions.bSubtitlesEnabled)
	{
		// Subtitles disabled by accessibility options: still route to TTS (audio accessibility) if asked,
		// but show nothing.
		MaybeSpeak(Line);
		return 0;
	}

	// De-duplicate an identical in-flight line: refresh its timer rather than stacking a copy.
	if (FSubtitleItem* Existing = FindDuplicate(Line))
	{
		Existing->TimeRemaining = ResolveDuration(Line);
		PushVisibleToViewModel();
		return Existing->InstanceId;
	}

	FSubtitleItem Item;
	Item.InstanceId = NextInstanceId++;
	Item.Line = Line;
	Item.Line.Duration = ResolveDuration(Line); // bake the concrete duration into the stored line
	Item.TimeRemaining = Item.Line.Duration;
	Item.bActive = false;

	const int64 NewId = Item.InstanceId;
	InsertByPriority(MoveTemp(Item));

	PromoteQueued();
	PushVisibleToViewModel();
	return NewId;
}

void ULoc_SubtitleSubsystem::ClearSubtitles()
{
	if (Items.Num() == 0)
	{
		return;
	}
	Items.Reset();
	PushVisibleToViewModel();
}

int32 ULoc_SubtitleSubsystem::ClearSubtitlesBySpeaker(FGameplayTag Speaker)
{
	if (!Speaker.IsValid())
	{
		return 0;
	}

	const int32 Removed = Items.RemoveAll([&Speaker](const FSubtitleItem& It)
	{
		return It.Line.Speaker.IsValid() && It.Line.Speaker.MatchesTag(Speaker);
	});

	if (Removed > 0)
	{
		PromoteQueued();
		PushVisibleToViewModel();
	}
	return Removed;
}

UDP_MessageBusSubsystem* ULoc_SubtitleSubsystem::GetBus() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr;
}

int32 ULoc_SubtitleSubsystem::ResolveMaxOnScreen() const
{
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const int32 Max = Settings ? Settings->MaxSubtitlesOnScreen : GFallbackMaxOnScreen;
	return FMath::Max(1, Max);
}

float ULoc_SubtitleSubsystem::ResolveDuration(const FLoc_SubtitleLine& Line) const
{
	if (Line.Duration > 0.f)
	{
		return Line.Duration;
	}

	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const float PerChar = Settings ? Settings->SubtitleSecondsPerCharacter : GFallbackSecondsPerChar;
	const float MinDur  = Settings ? Settings->SubtitleMinDuration          : GFallbackMinDuration;
	const float MaxDur  = Settings ? Settings->SubtitleMaxDuration          : GFallbackMaxDuration;

	const int32 Length = Line.Text.ToString().Len();
	const float Computed = Length * PerChar;
	return FMath::Clamp(Computed, MinDur, FMath::Max(MinDur, MaxDur));
}

int32 ULoc_SubtitleSubsystem::ResolvePriorityRank(const FGameplayTag& Priority) const
{
	// Map the canonical priority anchors to sortable ranks; higher shows first. An unset/unknown
	// priority is treated as standard dialogue (the documented fallback).
	if (Priority == DPLocTags::SubtitlePriority_Critical)
	{
		return 2;
	}
	if (Priority == DPLocTags::SubtitlePriority_Ambient)
	{
		return 0;
	}
	// DPLocTags::SubtitlePriority_Dialogue, an unset tag, or any other tag -> standard.
	return 1;
}

void ULoc_SubtitleSubsystem::SubscribeBus()
{
	UDP_MessageBusSubsystem* Bus = GetBus();
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_SubtitleSubsystem: no message bus; only the direct ShowSubtitle API is active."));
		return;
	}

	auto LineHandler = [this](const FDP_Message& Msg) { HandleLineMessage(Msg); };
	auto ClearHandler = [this](const FDP_Message& Msg) { HandleClearMessage(Msg); };

	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_DialogueLine, LineHandler, this));
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_VoiceLine,    LineHandler, this));
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_SubtitleShow, LineHandler, this));
	ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_SubtitleClear, ClearHandler, this));
}

void ULoc_SubtitleSubsystem::HandleLineMessage(const FDP_Message& Message)
{
	// Extract an FLoc_SubtitleLine from the payload. Ignore (verbose-log) payloads of the wrong type so a
	// mis-tagged broadcast cannot crash the caption pipeline.
	const FLoc_SubtitleLine* LinePtr = Message.Payload.GetPtr<FLoc_SubtitleLine>();
	if (!LinePtr)
	{
		UE_LOG(LogDP, Verbose, TEXT("ULoc_SubtitleSubsystem: line message on '%s' had a non-FLoc_SubtitleLine payload; ignored."),
			*Message.Channel.ToString());
		return;
	}
	ShowSubtitle(*LinePtr);
}

void ULoc_SubtitleSubsystem::HandleClearMessage(const FDP_Message& Message)
{
	// A tag payload clears by speaker; an empty/other payload clears all.
	if (const FGameplayTag* SpeakerPtr = Message.Payload.GetPtr<FGameplayTag>())
	{
		if (SpeakerPtr->IsValid())
		{
			ClearSubtitlesBySpeaker(*SpeakerPtr);
			return;
		}
	}
	ClearSubtitles();
}

void ULoc_SubtitleSubsystem::InsertByPriority(FSubtitleItem&& Item)
{
	const int32 Rank = ResolvePriorityRank(Item.Line.Priority);

	// Find the first existing item with a strictly lower rank; insert before it (stable within a rank).
	int32 InsertAt = Items.Num();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (ResolvePriorityRank(Items[i].Line.Priority) < Rank)
		{
			InsertAt = i;
			break;
		}
	}
	Items.Insert(MoveTemp(Item), InsertAt);
}

ULoc_SubtitleSubsystem::FSubtitleItem* ULoc_SubtitleSubsystem::FindDuplicate(const FLoc_SubtitleLine& Line)
{
	for (FSubtitleItem& It : Items)
	{
		if (It.Line.Speaker == Line.Speaker && It.Line.Text.EqualTo(Line.Text))
		{
			return &It;
		}
	}
	return nullptr;
}

void ULoc_SubtitleSubsystem::PromoteQueued()
{
	const int32 Cap = ResolveMaxOnScreen();
	int32 ActiveCount = 0;

	for (FSubtitleItem& It : Items)
	{
		if (ActiveCount >= Cap)
		{
			break;
		}

		if (!It.bActive)
		{
			It.bActive = true;
			// (Re)arm the timer if it has not been started.
			if (It.TimeRemaining <= 0.f)
			{
				It.TimeRemaining = It.Line.Duration;
			}
			// Newly surfaced — route to TTS if requested/available.
			MaybeSpeak(It.Line);
		}
		++ActiveCount;
	}
}

bool ULoc_SubtitleSubsystem::TickSubtitles(float DeltaTime)
{
	if (Items.Num() == 0)
	{
		return true; // keep the ticker alive for future lines
	}

	bool bChanged = false;

	// Decrement active timers; collect expired instance ids.
	for (FSubtitleItem& It : Items)
	{
		if (It.bActive)
		{
			It.TimeRemaining -= DeltaTime;
		}
	}

	const int32 RemovedExpired = Items.RemoveAll([](const FSubtitleItem& It)
	{
		return It.bActive && It.TimeRemaining <= 0.f;
	});

	if (RemovedExpired > 0)
	{
		bChanged = true;
		PromoteQueued();
	}

	// Even with no removals, active TimeRemaining changed; push so the UI countdown stays live.
	PushVisibleToViewModel();
	(void)bChanged;
	return true;
}

void ULoc_SubtitleSubsystem::PushVisibleToViewModel()
{
	if (!ViewModel)
	{
		return;
	}

	TArray<FLoc_ActiveSubtitleView> Visible;
	Visible.Reserve(Items.Num());

	for (const FSubtitleItem& It : Items)
	{
		if (!It.bActive)
		{
			continue;
		}
		FLoc_ActiveSubtitleView View;
		View.InstanceId = It.InstanceId;
		View.Line = It.Line;
		View.TimeRemaining = FMath::Max(0.f, It.TimeRemaining);
		Visible.Add(MoveTemp(View));
	}

	ViewModel->SetVisibleSubtitles(Visible);
}

void ULoc_SubtitleSubsystem::PushAccessibilityToViewModel()
{
	if (ViewModel)
	{
		ViewModel->SetAccessibilityPresentation(
			CurrentOptions.bSubtitlesEnabled,
			CurrentOptions.SubtitleSize,
			CurrentOptions.bSubtitleBackground);
	}
}

ISeam_TextToSpeech* ULoc_SubtitleSubsystem::ResolveTTS() const
{
	// Fast path: still-valid cached backend.
	if (CachedTTS.IsValid())
	{
		return CachedTTS.Get();
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}

	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(DPLocTags::Service_TextToSpeech);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(USeam_TextToSpeech::StaticClass()))
	{
		return nullptr;
	}

	// Cache weakly so a TTS backend owned elsewhere is not kept alive across worlds by this subsystem.
	const_cast<ULoc_SubtitleSubsystem*>(this)->CachedTTS = TWeakInterfacePtr<ISeam_TextToSpeech>(*Provider);
	return CachedTTS.Get();
}

void ULoc_SubtitleSubsystem::MaybeSpeak(const FLoc_SubtitleLine& Line)
{
	if (!CurrentOptions.bTextToSpeechEnabled)
	{
		return;
	}

	ISeam_TextToSpeech* TTS = ResolveTTS();
	if (!TTS)
	{
		// Inert default: no backend -> silent.
		return;
	}

	UObject* TTSObject = CachedTTS.GetObject();
	if (!TTSObject)
	{
		return;
	}

	// Gate on availability (the backend may be registered but not currently able to speak).
	if (!ISeam_TextToSpeech::Execute_IsSpeechAvailable(TTSObject))
	{
		return;
	}

	ISeam_TextToSpeech::Execute_Speak(TTSObject, Line.Text, DPLocTags::TTS_Subtitle);
}

FString ULoc_SubtitleSubsystem::GetDPDebugString_Implementation() const
{
	int32 ActiveCount = 0;
	for (const FSubtitleItem& It : Items)
	{
		if (It.bActive)
		{
			++ActiveCount;
		}
	}
	const int32 Queued = Items.Num() - ActiveCount;
	return FString::Printf(TEXT("Subtitles: enabled=%s active=%d queued=%d tts=%s"),
		CurrentOptions.bSubtitlesEnabled ? TEXT("true") : TEXT("false"),
		ActiveCount, Queued,
		CurrentOptions.bTextToSpeechEnabled ? TEXT("on") : TEXT("off"));
}
