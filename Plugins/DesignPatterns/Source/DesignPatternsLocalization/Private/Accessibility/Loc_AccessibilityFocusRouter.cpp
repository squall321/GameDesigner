// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_AccessibilityFocusRouter.h"

#include "Accessibility/Loc_AccessibilitySubsystem.h"
#include "Subtitle/Loc_RichSubtitleTypes.h"
#include "Settings/Loc_DeveloperSettings.h"
#include "DesignPatternsLocalizationModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Input/Seam_InputGlyphProvider.h"

#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

// FInstancedStruct: StructUtils on 5.3/5.4, CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace Loc_FocusRouterPrivate
{
	/** Default debounce when settings are unavailable: prevents TTS spam on rapid focus changes. */
	constexpr float GFallbackDebounceSeconds = 0.25f;
}

// ------------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------------

void ULoc_AccessibilityFocusRouter::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Subscribe to the UI-focus channel so we receive FLoc_UIFocusEvent when a widget is focused.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		auto Handler = [this](const FDP_Message& Msg) { HandleUIFocus(Msg); };
		ListenerHandles.Add(Bus->ListenNative(DPLocTags::Bus_UIFocusChanged, Handler, this));
	}
	else
	{
		UE_LOG(LogDP, Verbose,
			TEXT("ULoc_AccessibilityFocusRouter: no message bus at Initialize; UI focus TTS will be silent."));
	}

	// Register as an accessibility consumer so the accessibility subsystem pushes TTS-enabled state to us.
	RegisterWithAccessibilityProvider();

	UE_LOG(LogDP, Log, TEXT("ULoc_AccessibilityFocusRouter initialized (TTS enabled=%s, debounce=%.2fs)."),
		CurrentOptions.bTextToSpeechEnabled ? TEXT("yes") : TEXT("no"),
		ResolveDebounceSeconds());
}

void ULoc_AccessibilityFocusRouter::Deinitialize()
{
	UnregisterFromAccessibilityProvider();

	// Remove all bus subscriptions (bus may already be gone on world teardown; null-safe).
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}
	ListenerHandles.Reset();

	Super::Deinitialize();
}

// ------------------------------------------------------------------------------------------------
// ISeam_AccessibilityConsumer
// ------------------------------------------------------------------------------------------------

void ULoc_AccessibilityFocusRouter::OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options)
{
	CurrentOptions = Options;

	// If TTS just got disabled, stop any in-flight speech so the player isn't left with stale audio.
	if (!Options.bTextToSpeechEnabled)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (ULoc_AccessibilitySubsystem* Access = GI->GetSubsystem<ULoc_AccessibilitySubsystem>())
			{
				Access->StopSpeaking();
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------------------------------------

void ULoc_AccessibilityFocusRouter::SpeakFocus(const FText& AccessibleText)
{
	if (!CurrentOptions.bTextToSpeechEnabled)
	{
		// TTS is off — no-op (inert default).
		return;
	}

	// Debounce: discard rapid calls within the configured window so a screen reader is not spammed.
	const float DebounceSeconds = ResolveDebounceSeconds();
	const double Now = FApp::GetCurrentTime();
	if ((Now - LastSpeakTime) < static_cast<double>(DebounceSeconds))
	{
		UE_LOG(LogDP, Verbose,
			TEXT("ULoc_AccessibilityFocusRouter: focus speak suppressed by debounce (%.2f < %.2f s)."),
			static_cast<float>(Now - LastSpeakTime), DebounceSeconds);
		return;
	}
	LastSpeakTime = Now;

	// Route to the existing ULoc_AccessibilitySubsystem::SpeakText under the UIFocus TTS category.
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}
	ULoc_AccessibilitySubsystem* Access = GI->GetSubsystem<ULoc_AccessibilitySubsystem>();
	if (!Access)
	{
		return;
	}

	Access->SpeakText(AccessibleText, DPLocTags::TTS_UIFocus);
}

// ------------------------------------------------------------------------------------------------
// Debug
// ------------------------------------------------------------------------------------------------

FString ULoc_AccessibilityFocusRouter::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("FocusRouter: TTS=%s debounce=%.2fs lastSpeak=%.2f"),
		CurrentOptions.bTextToSpeechEnabled ? TEXT("on") : TEXT("off"),
		ResolveDebounceSeconds(),
		static_cast<float>(LastSpeakTime));
}

// ------------------------------------------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------------------------------------------

void ULoc_AccessibilityFocusRouter::HandleUIFocus(const FDP_Message& Message)
{
	const FLoc_UIFocusEvent* EventPtr = Message.Payload.GetPtr<FLoc_UIFocusEvent>();
	if (!EventPtr)
	{
		return; // wrong payload type — someone else's bus message on this channel.
	}

	if (!CurrentOptions.bTextToSpeechEnabled)
	{
		return; // TTS is off; skip work.
	}

	FText SpeakText = EventPtr->FocusText;

	// Optionally prepend the binding label for the focused action (e.g. "[A] Confirm") via the input-glyph
	// seam, so a player using a gamepad hears the button label alongside the widget label.
	if (EventPtr->ActionTag.IsValid())
	{
		const FText BindingLabel = ResolveBindingLabel(EventPtr->ActionTag);
		if (!BindingLabel.IsEmpty())
		{
			// Assemble: "[<binding>] <focus text>", culture-neutral (bracket / space) for QA simplicity.
			SpeakText = FText::Format(NSLOCTEXT("DP.Loc", "FocusWithBinding", "[{0}] {1}"), BindingLabel, SpeakText);
		}
	}

	SpeakFocus(SpeakText);
}

UDP_MessageBusSubsystem* ULoc_AccessibilityFocusRouter::GetBus() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr;
}

void ULoc_AccessibilityFocusRouter::RegisterWithAccessibilityProvider()
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

	ULoc_AccessibilitySubsystem* Access = GI->GetSubsystem<ULoc_AccessibilitySubsystem>();
	if (!Access)
	{
		// Inert: no accessibility subsystem — we remain at compiled-in defaults (TTS off).
		return;
	}

	TScriptInterface<ISeam_AccessibilityConsumer> Self;
	Self.SetObject(this);
	Self.SetInterface(Cast<ISeam_AccessibilityConsumer>(this));
	Access->RegisterConsumer(Self);
	bRegisteredAccessibility = true;

	UE_LOG(LogDP, Verbose, TEXT("ULoc_AccessibilityFocusRouter: registered as accessibility consumer."));
}

void ULoc_AccessibilityFocusRouter::UnregisterFromAccessibilityProvider()
{
	if (!bRegisteredAccessibility)
	{
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (GI)
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

FText ULoc_AccessibilityFocusRouter::ResolveBindingLabel(FGameplayTag ActionTag) const
{
	if (!ActionTag.IsValid())
	{
		return FText::GetEmpty();
	}

	// Resolve the ISeam_InputGlyphProvider from the service locator (the Glyphs service key).
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return FText::GetEmpty();
	}

	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return FText::GetEmpty();
	}

	// Resolve the canonical input-glyph provider service (DP.Service.Platform.Glyphs).
	UObject* Provider = Locator->ResolveService(DPLocTags::Service_InputGlyphs);
	if (!Provider || !Provider->Implements<USeam_InputGlyphProvider>())
	{
		return FText::GetEmpty();
	}

	// ResolveActionGlyph signature: (FGameplayTag, TSoftObjectPtr<UTexture2D>&, FText&) const.
	// We only want the label (OutLabel); ignore the glyph texture.
	TSoftObjectPtr<UTexture2D> OutGlyph;
	FText OutLabel;
	ISeam_InputGlyphProvider::Execute_ResolveActionGlyph(Provider, ActionTag, OutGlyph, OutLabel);

	return OutLabel;
}

float ULoc_AccessibilityFocusRouter::ResolveDebounceSeconds() const
{
	const ULoc_DeveloperSettings* Settings = ULoc_DeveloperSettings::Get();
	const float Raw = Settings ? Settings->FocusSpeakDebounceSeconds : Loc_FocusRouterPrivate::GFallbackDebounceSeconds;
	return FMath::Max(0.0f, Raw);
}
