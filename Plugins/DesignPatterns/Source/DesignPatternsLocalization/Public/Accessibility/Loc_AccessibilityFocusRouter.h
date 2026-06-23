// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "MessageBus/DPMessage.h"
#include "Loc_AccessibilityFocusRouter.generated.h"

class UDP_MessageBusSubsystem;
struct FDP_Message;

/**
 * GameInstance-scoped screen-reader focus router.
 *
 * Subscribes to DPLocTags::Bus_UIFocusChanged (payload FLoc_UIFocusEvent: focus FText + category + optional
 * action tag) broadcast by UI when the focused widget changes, and — when the player has text-to-speech
 * enabled — routes the focus text to the EXISTING ULoc_AccessibilitySubsystem::SpeakText(FText, Category)
 * under the DP.Loc.TTS.UIFocus category. It debounces rapid focus changes (per settings) so a screen reader
 * is not spammed, and optionally prepends the input binding label for the focused element by resolving the
 * EXISTING ISeam_InputGlyphProvider (ResolveActionGlyph) from the service locator.
 *
 * Implements ISeam_AccessibilityConsumer so the accessibility subsystem pushes whether TTS is enabled.
 *
 * LOCAL / per-machine — nothing replicates. GC: GameInstance subsystem; the bus listener + the accessibility
 * registration + the debounce timer are all removed on Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_AccessibilityFocusRouter : public UDP_GameInstanceSubsystem, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AccessibilityConsumer
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/** Directly speak AccessibleText as a focus utterance (respects the TTS-enabled option + debounce). */
	UFUNCTION(BlueprintCallable, Category = "Localization|Accessibility")
	void SpeakFocus(const FText& AccessibleText);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Bus handler for Bus_UIFocusChanged: extracts the FLoc_UIFocusEvent and routes it to TTS. */
	void HandleUIFocus(const FDP_Message& Message);

	/** Resolve the game-instance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Register/unregister as an accessibility consumer (null-safe). */
	void RegisterWithAccessibilityProvider();
	void UnregisterFromAccessibilityProvider();

	/** Resolve the binding label for ActionTag via the input-glyph seam (empty if unresolved). */
	FText ResolveBindingLabel(FGameplayTag ActionTag) const;

	/** Resolve the configured debounce seconds (defensive fallback). */
	float ResolveDebounceSeconds() const;

	/** Current accessibility options (defaults to all-on; bTextToSpeechEnabled defaults false). */
	FSeam_AccessibilityOptions CurrentOptions;

	/** Bus listener handles, removed on Deinitialize. */
	TArray<FDP_ListenerHandle> ListenerHandles;

	/** Last app time (FApp::GetCurrentTime) a focus utterance was spoken — drives debounce. */
	double LastSpeakTime = 0.0;

	/** True once registered as an accessibility consumer. */
	bool bRegisteredAccessibility = false;
};
