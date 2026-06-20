// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_TextToSpeech.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Loc_AccessibilitySubsystem.generated.h"

class UObject;

/**
 * Multicast fired (game thread) after the authoritative options change and have been pushed to every
 * live consumer. Blueprint/native systems that prefer an event over implementing ISeam_AccessibilityConsumer
 * can bind here. The payload is the new, complete option set.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoc_OnAccessibilityOptionsChanged, const FSeam_AccessibilityOptions&, NewOptions);

/**
 * GameInstance-scoped owner of the player's current accessibility options.
 *
 * RESPONSIBILITIES:
 *   - Holds the single authoritative FSeam_AccessibilityOptions for the local machine.
 *   - Exposes GetOptions()/SetOptions() plus per-field setters; any mutation that actually changes a
 *     value persists to ULoc_GameUserSettings and pushes the full struct to every registered consumer.
 *   - Maintains the registry of ISeam_AccessibilityConsumer implementers (UI/Camera/HUD) and notifies
 *     them on register and on every change so all systems react uniformly.
 *   - When bTextToSpeechEnabled, routes SpeakText() to a resolved ISeam_TextToSpeech backend; silent
 *     (no-op) when the seam is unset, keeping the module inert-by-default and independently removable.
 *
 * GC / WORLD-LIFETIME SAFETY (HARD RULE):
 *   This is a GameInstance subsystem, so it outlives individual worlds. It must NOT hold TScriptInterface
 *   hard references to consumers or to the TTS backend — a hard ref to a world-scoped UObject would keep a
 *   dead world's object alive (or dangle) and crash on level travel. Therefore consumers are stored as
 *   PARALLEL arrays of TWeakObjectPtr<UObject> + raw interface pointer, validated and pruned on every push,
 *   and the TTS backend is stored as TWeakInterfacePtr resolved from the service locator and re-checked
 *   on use.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLOCALIZATION_API ULoc_AccessibilitySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Convenience accessor from any world-context object. Null-safe at every hop; may return null. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static ULoc_AccessibilitySubsystem* Get(const UObject* WorldContextObject);

	/** Fired (game thread) after options change and have been pushed to all live consumers. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Accessibility")
	FLoc_OnAccessibilityOptionsChanged OnAccessibilityOptionsChanged;

	// --- Options access ---

	/** The current authoritative options (a copy; consumers should not mutate the source). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	const FSeam_AccessibilityOptions& GetOptions() const { return CurrentOptions; }

	/**
	 * Replace the entire option set. If the new value differs from the current one it is persisted and
	 * broadcast. Passing an identical struct is a cheap no-op (no save, no broadcast). Returns true if a
	 * change was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetOptions(const FSeam_AccessibilityOptions& NewOptions);

	// Per-field setters. Each commits + broadcasts only if the field actually changes. Each returns true
	// if a change was applied. These exist so a settings widget can bind one control per field without
	// round-tripping the whole struct.

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetSubtitlesEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetSubtitleSize(ESeam_SubtitleSize Size);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetSubtitleBackground(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetColorblindMode(ESeam_ColorblindMode Mode);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetUIScale(float Scale);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetHoldToToggle(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetScreenShakeScale(float Scale);

	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SetTextToSpeechEnabled(bool bEnabled);

	// --- Consumer registry ---

	/**
	 * Register a consumer. It is immediately pushed the current options (so it starts in sync) and will
	 * receive every subsequent change until unregistered or GC'd. Stored weakly; if Consumer is later
	 * destroyed it is pruned silently on the next push. Re-registering the same object is a no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	void RegisterConsumer(const TScriptInterface<ISeam_AccessibilityConsumer>& Consumer);

	/** Remove a previously-registered consumer. Safe if it was never registered. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	void UnregisterConsumer(const TScriptInterface<ISeam_AccessibilityConsumer>& Consumer);

	// --- Text-to-speech routing ---

	/**
	 * Route Text to the resolved TTS backend, IFF bTextToSpeechEnabled AND a backend is connected and
	 * available. Silent no-op otherwise. Category lets the backend route/duck (UI vs narration). Returns
	 * true if the text was handed to a backend.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	bool SpeakText(const FText& Text, FGameplayTag Category);

	/** Stop any in-progress speech on the resolved backend (no-op if unset). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Accessibility")
	void StopSpeaking();

	/** True if a real TTS backend is currently resolved and reports itself available. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	bool IsTextToSpeechAvailable() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/**
	 * Commit a changed option set: write through to ULoc_GameUserSettings (best-effort persistence) and
	 * push to every live consumer + the delegate. Called by SetOptions and every per-field setter after a
	 * real change. bPersist=false is used during init (options loaded FROM settings shouldn't be re-saved).
	 */
	void CommitOptions(bool bPersist);

	/** Push CurrentOptions to all live consumers, pruning any whose weak object has gone stale. */
	void BroadcastToConsumers();

	/** Load CurrentOptions from ULoc_GameUserSettings if available; otherwise keep struct defaults. */
	void LoadOptionsFromSettings();

	/** Resolve the TTS backend from the service locator into TextToSpeechBackend (weak). Re-resolves if stale. */
	ISeam_TextToSpeech* ResolveTextToSpeech();

	/** The single authoritative option set for this machine. */
	UPROPERTY(Transient)
	FSeam_AccessibilityOptions CurrentOptions;

	/**
	 * Registered consumer objects, stored WEAK (never a TScriptInterface hard ref in a GI subsystem — that
	 * outlives worlds and crashes). Parallel to ConsumerInterfaces by index. Pruned on every broadcast.
	 */
	TArray<TWeakObjectPtr<UObject>> ConsumerObjects;

	/**
	 * Raw interface pointers parallel to ConsumerObjects. Only dereferenced after the matching weak object
	 * pointer validates as live in the same iteration, so the raw pointer is never used while dangling.
	 */
	TArray<ISeam_AccessibilityConsumer*> ConsumerInterfaces;

	/** Weakly-held TTS backend resolved from the service locator. Inert (silent) when unset/stale. */
	TWeakInterfacePtr<ISeam_TextToSpeech> TextToSpeechBackend;

	/** True once Initialize has loaded persisted options, so early setters don't broadcast pre-init. */
	bool bInitialized = false;
};
