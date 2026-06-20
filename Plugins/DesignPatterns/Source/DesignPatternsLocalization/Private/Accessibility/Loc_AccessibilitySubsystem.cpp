// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_AccessibilitySubsystem.h"

#include "Accessibility/Loc_GameUserSettings.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Engine/GameInstance.h"

namespace Loc_AccessibilityPrivate
{
	/**
	 * Service-locator key under which the host project registers its ISeam_TextToSpeech backend.
	 * Resolved with ErrorIfNotFound=false so the tag need not be pre-authored: if the project never
	 * registers a backend, the key simply resolves to an empty tag / null provider and TTS stays silent.
	 */
	static FGameplayTag GetTextToSpeechServiceKey()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Localization.TextToSpeech")), /*ErrorIfNotFound*/ false);
	}
}

void ULoc_AccessibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Pull persisted options from the game user settings (or keep struct defaults if not configured).
	LoadOptionsFromSettings();
	bInitialized = true;

	UE_LOG(LogDP, Log, TEXT("Loc_AccessibilitySubsystem initialized. Subtitles=%s UIScale=%.2f Colorblind=%d TTS=%s"),
		CurrentOptions.bSubtitlesEnabled ? TEXT("on") : TEXT("off"),
		CurrentOptions.UIScale,
		static_cast<int32>(CurrentOptions.ColorblindMode),
		CurrentOptions.bTextToSpeechEnabled ? TEXT("on") : TEXT("off"));
}

void ULoc_AccessibilitySubsystem::Deinitialize()
{
	// Best-effort: stop any speech before tearing down so a backend isn't left talking across travel.
	StopSpeaking();

	ConsumerObjects.Reset();
	ConsumerInterfaces.Reset();
	TextToSpeechBackend.Reset();
	bInitialized = false;

	Super::Deinitialize();
}

ULoc_AccessibilitySubsystem* ULoc_AccessibilitySubsystem::Get(const UObject* WorldContextObject)
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<ULoc_AccessibilitySubsystem>(WorldContextObject);
}

void ULoc_AccessibilitySubsystem::LoadOptionsFromSettings()
{
	if (ULoc_GameUserSettings* Settings = ULoc_GameUserSettings::GetLocGameUserSettings())
	{
		CurrentOptions = Settings->GetAccessibilityOptions();
	}
	else
	{
		// Documented defensive fallback: project didn't set GameUserSettingsClassName to Loc_GameUserSettings.
		// We run with the struct's compiled-in defaults; options simply won't persist across runs.
		UE_LOG(LogDP, Verbose,
			TEXT("Loc_AccessibilitySubsystem: ULoc_GameUserSettings not active (GameUserSettingsClassName unset). ")
			TEXT("Using in-memory accessibility defaults; options will not persist."));
	}
}

bool ULoc_AccessibilitySubsystem::SetOptions(const FSeam_AccessibilityOptions& NewOptions)
{
	// Cheap structural equality check via property comparison; avoids a needless save+broadcast.
	const FSeam_AccessibilityOptions& Old = CurrentOptions;
	const bool bChanged =
		Old.bSubtitlesEnabled   != NewOptions.bSubtitlesEnabled  ||
		Old.SubtitleSize        != NewOptions.SubtitleSize       ||
		Old.bSubtitleBackground != NewOptions.bSubtitleBackground||
		Old.ColorblindMode      != NewOptions.ColorblindMode     ||
		!FMath::IsNearlyEqual(Old.UIScale, NewOptions.UIScale)   ||
		Old.bHoldToToggle       != NewOptions.bHoldToToggle      ||
		!FMath::IsNearlyEqual(Old.ScreenShakeScale, NewOptions.ScreenShakeScale) ||
		Old.bTextToSpeechEnabled!= NewOptions.bTextToSpeechEnabled;

	if (!bChanged)
	{
		return false;
	}

	CurrentOptions = NewOptions;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetSubtitlesEnabled(bool bEnabled)
{
	if (CurrentOptions.bSubtitlesEnabled == bEnabled) { return false; }
	CurrentOptions.bSubtitlesEnabled = bEnabled;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetSubtitleSize(ESeam_SubtitleSize Size)
{
	if (CurrentOptions.SubtitleSize == Size) { return false; }
	CurrentOptions.SubtitleSize = Size;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetSubtitleBackground(bool bEnabled)
{
	if (CurrentOptions.bSubtitleBackground == bEnabled) { return false; }
	CurrentOptions.bSubtitleBackground = bEnabled;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetColorblindMode(ESeam_ColorblindMode Mode)
{
	if (CurrentOptions.ColorblindMode == Mode) { return false; }
	CurrentOptions.ColorblindMode = Mode;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetUIScale(float Scale)
{
	// Clamp to the same range the struct's UPROPERTY meta documents, so an out-of-range BP/script call
	// can't desync the persisted value from what the editor would allow.
	const float Clamped = FMath::Clamp(Scale, 0.5f, 2.0f);
	if (FMath::IsNearlyEqual(CurrentOptions.UIScale, Clamped)) { return false; }
	CurrentOptions.UIScale = Clamped;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetHoldToToggle(bool bEnabled)
{
	if (CurrentOptions.bHoldToToggle == bEnabled) { return false; }
	CurrentOptions.bHoldToToggle = bEnabled;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetScreenShakeScale(float Scale)
{
	const float Clamped = FMath::Clamp(Scale, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(CurrentOptions.ScreenShakeScale, Clamped)) { return false; }
	CurrentOptions.ScreenShakeScale = Clamped;
	CommitOptions(/*bPersist*/ true);
	return true;
}

bool ULoc_AccessibilitySubsystem::SetTextToSpeechEnabled(bool bEnabled)
{
	if (CurrentOptions.bTextToSpeechEnabled == bEnabled) { return false; }
	CurrentOptions.bTextToSpeechEnabled = bEnabled;

	// If the player just turned TTS off, stop any speech immediately rather than waiting for the backend.
	if (!bEnabled)
	{
		StopSpeaking();
	}

	CommitOptions(/*bPersist*/ true);
	return true;
}

void ULoc_AccessibilitySubsystem::CommitOptions(bool bPersist)
{
	if (bPersist)
	{
		// Write-through to the game user settings backing store, if configured. Best-effort: a project
		// without ULoc_GameUserSettings simply keeps the in-memory value (documented fallback).
		if (ULoc_GameUserSettings* Settings = ULoc_GameUserSettings::GetLocGameUserSettings())
		{
			if (Settings->SetAccessibilityOptions(CurrentOptions))
			{
				Settings->SaveSettings();
			}
		}
	}

	BroadcastToConsumers();
	OnAccessibilityOptionsChanged.Broadcast(CurrentOptions);
}

void ULoc_AccessibilitySubsystem::BroadcastToConsumers()
{
	// Iterate back-to-front so we can RemoveAtSwap stale entries without disturbing not-yet-visited indices.
	for (int32 Index = ConsumerObjects.Num() - 1; Index >= 0; --Index)
	{
		UObject* Obj = ConsumerObjects[Index].Get();

		// Prune ONLY when the weak object has been GC'd. A null native interface pointer is NOT a staleness
		// signal: a Blueprint-only implementation of ISeam_AccessibilityConsumer yields a valid object with a
		// null native interface pointer, and Execute_ below routes correctly to both C++ and BP. The weak
		// object is the sole truth for liveness. (Single-arg RemoveAtSwap for 5.3-5.5 source compatibility.)
		if (!Obj)
		{
			ConsumerObjects.RemoveAtSwap(Index);
			if (ConsumerInterfaces.IsValidIndex(Index))
			{
				ConsumerInterfaces.RemoveAtSwap(Index);
			}
			continue;
		}

		// Route through the UFunction so BlueprintNativeEvent consumers (C++ or BP) both receive it.
		ISeam_AccessibilityConsumer::Execute_OnAccessibilityOptionsChanged(Obj, CurrentOptions);
	}
}

void ULoc_AccessibilitySubsystem::RegisterConsumer(const TScriptInterface<ISeam_AccessibilityConsumer>& Consumer)
{
	UObject* Obj = Consumer.GetObject();
	ISeam_AccessibilityConsumer* Iface = Consumer.GetInterface();

	// A BP-implemented interface yields a valid object but a null native interface pointer; that's fine,
	// we route via Execute_. Reject only a null object.
	if (!Obj)
	{
		UE_LOG(LogDP, Warning, TEXT("Loc_AccessibilitySubsystem::RegisterConsumer ignored a null consumer."));
		return;
	}

	// De-dupe by object identity (also a chance to prune any stale slot for the same index space).
	for (const TWeakObjectPtr<UObject>& Existing : ConsumerObjects)
	{
		if (Existing.Get() == Obj)
		{
			return; // already registered
		}
	}

	ConsumerObjects.Add(Obj);
	ConsumerInterfaces.Add(Iface);

	// Push current state immediately so the new consumer starts in sync.
	ISeam_AccessibilityConsumer::Execute_OnAccessibilityOptionsChanged(Obj, CurrentOptions);
}

void ULoc_AccessibilitySubsystem::UnregisterConsumer(const TScriptInterface<ISeam_AccessibilityConsumer>& Consumer)
{
	const UObject* Obj = Consumer.GetObject();
	if (!Obj)
	{
		return;
	}

	for (int32 Index = ConsumerObjects.Num() - 1; Index >= 0; --Index)
	{
		if (ConsumerObjects[Index].Get() == Obj)
		{
			ConsumerObjects.RemoveAtSwap(Index);
			if (ConsumerInterfaces.IsValidIndex(Index))
			{
				ConsumerInterfaces.RemoveAtSwap(Index);
			}
			return;
		}
	}
}

ISeam_TextToSpeech* ULoc_AccessibilitySubsystem::ResolveTextToSpeech()
{
	// Fast path: already resolved and still live.
	if (TextToSpeechBackend.IsValid())
	{
		return TextToSpeechBackend.GetInterface();
	}

	// (Re)resolve from the service locator. Held weakly so a world-scoped backend cannot leak across travel.
	TextToSpeechBackend.Reset();

	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}

	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	const FGameplayTag Key = Loc_AccessibilityPrivate::GetTextToSpeechServiceKey();
	if (!Key.IsValid())
	{
		return nullptr; // project never authored/registered a TTS service tag -> stay silent.
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->Implements<USeam_TextToSpeech>())
	{
		TextToSpeechBackend = TWeakInterfacePtr<ISeam_TextToSpeech>(*Provider);
		return TextToSpeechBackend.GetInterface();
	}

	return nullptr;
}

bool ULoc_AccessibilitySubsystem::SpeakText(const FText& Text, FGameplayTag Category)
{
	// Inert by default: nothing happens unless the player enabled TTS AND a backend is connected.
	if (!CurrentOptions.bTextToSpeechEnabled)
	{
		return false;
	}

	ISeam_TextToSpeech* Backend = ResolveTextToSpeech();
	UObject* BackendObj = TextToSpeechBackend.GetObject();
	if (!Backend || !BackendObj)
	{
		return false;
	}

	if (!ISeam_TextToSpeech::Execute_IsSpeechAvailable(BackendObj))
	{
		return false;
	}

	ISeam_TextToSpeech::Execute_Speak(BackendObj, Text, Category);
	return true;
}

void ULoc_AccessibilitySubsystem::StopSpeaking()
{
	if (UObject* BackendObj = TextToSpeechBackend.GetObject())
	{
		ISeam_TextToSpeech::Execute_StopSpeaking(BackendObj);
	}
}

bool ULoc_AccessibilitySubsystem::IsTextToSpeechAvailable() const
{
	// Const accessor: don't mutate the cached weak handle here, just probe whatever is currently resolved.
	if (UObject* BackendObj = TextToSpeechBackend.GetObject())
	{
		return ISeam_TextToSpeech::Execute_IsSpeechAvailable(BackendObj);
	}
	return false;
}

FString ULoc_AccessibilitySubsystem::GetDPDebugString_Implementation() const
{
	int32 LiveConsumers = 0;
	for (const TWeakObjectPtr<UObject>& Weak : ConsumerObjects)
	{
		if (Weak.IsValid())
		{
			++LiveConsumers;
		}
	}

	return FString::Printf(
		TEXT("Accessibility: Subtitles=%s(Size=%d,BG=%s) Colorblind=%d UIScale=%.2f Shake=%.2f Hold2Toggle=%s TTS=%s(avail=%s) Consumers=%d"),
		CurrentOptions.bSubtitlesEnabled ? TEXT("on") : TEXT("off"),
		static_cast<int32>(CurrentOptions.SubtitleSize),
		CurrentOptions.bSubtitleBackground ? TEXT("on") : TEXT("off"),
		static_cast<int32>(CurrentOptions.ColorblindMode),
		CurrentOptions.UIScale,
		CurrentOptions.ScreenShakeScale,
		CurrentOptions.bHoldToToggle ? TEXT("on") : TEXT("off"),
		CurrentOptions.bTextToSpeechEnabled ? TEXT("on") : TEXT("off"),
		IsTextToSpeechAvailable() ? TEXT("yes") : TEXT("no"),
		LiveConsumers);
}
