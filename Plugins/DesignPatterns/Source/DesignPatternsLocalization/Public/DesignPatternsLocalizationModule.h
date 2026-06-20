// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * DesignPatternsLocalization module interface.
 *
 * Localization + subtitles + accessibility-aware presentation, wrapping the engine's FText /
 * FStringTableRegistry / FInternationalization culture machinery — this module NEVER reinvents FText.
 * It provides a clean, tag-keyed API over the engine systems:
 *  - ULoc_LocalizationSubsystem: get/set the active culture (FInternationalization::SetCurrentCulture),
 *    enumerate available cultures, and resolve designer-authored FGameplayTag keys to FText pulled from
 *    a configured string table (FText::FromStringTable). Fires OnCultureChanged so other systems re-pull.
 *  - ULoc_SubtitleSubsystem: a priority/cap subtitle queue fed by the message bus (dialogue/voice
 *    channels) and a direct ShowSubtitle API, honoring the shared FSeam_AccessibilityOptions (enabled /
 *    size / background) and optionally routing lines to the ISeam_TextToSpeech seam.
 *  - ULoc_SubtitleViewModel: the field-notification ViewModel the subtitle UI binds to.
 *
 * The module is LOCAL / per-machine (culture and subtitles are presentation, not gameplay) and is
 * independently removable: every cross-module hop is through a Seams interface resolved from the service
 * locator, and each seam has a documented inert default (TTS silent, accessibility = all-on defaults).
 *
 * This module's native GameplayTags live below in namespace DPLocTags — the single tag registry for the
 * whole Localization module. New tags are added here, not scattered across headers.
 */
class FDesignPatternsLocalizationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

/**
 * Native anchor tags owned by the DesignPatternsLocalization module, rooted at DP.Loc / DP.Bus.Loc /
 * DP.Service.Loc. These are the tags this module's own code references literally at runtime:
 *  - service-locator keys this module PUBLISHES (the live localization subsystem) and RESOLVES (the
 *    shared accessibility-options provider + the text-to-speech seam, both owned elsewhere);
 *  - the message-bus channels the subtitle subsystem LISTENS on (dialogue / voice lines) and the direct
 *    show/clear request channels;
 *  - a small set of canonical TTS category + subtitle-priority anchor tags so the presentation layer has
 *    stable, shipped keys.
 *
 * Everything designer-facing (per-game string-table ids, per-game speaker tags, per-game subtitle keys)
 * flows through settings / data assets as opaque FGameplayTag / FName values and is NEVER hard-coded
 * here. This header only anchors the tags this module mentions by name.
 */
namespace DPLocTags
{
	// --- Service-locator keys ---

	/** Service-locator key under which the live ULoc_LocalizationSubsystem publishes itself (weak-observed). */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Localization);

	/**
	 * Service-locator key under which the shared accessibility-options provider publishes itself. The
	 * subtitle subsystem resolves a TScriptInterface<ISeam_AccessibilityConsumer>-style provider here to
	 * register for option-change pushes. The provider is owned elsewhere (the accessibility subsystem);
	 * this module only references the key. Inert default when unresolved: subtitles render with all-on
	 * accessibility defaults.
	 */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AccessibilityProvider);

	/**
	 * Service-locator key under which the host project registers its ISeam_TextToSpeech backend. Resolved
	 * weakly; inert (silent) default when unresolved so the framework never depends on a TTS implementation.
	 */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_TextToSpeech);

	// --- Message-bus channels the subtitle subsystem listens on / publishes ---

	/** Dialogue-line channel: payloads carry an FLoc_SubtitleLine to surface as a subtitle. */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_DialogueLine);

	/** Voice/VO-line channel: payloads carry an FLoc_SubtitleLine to surface as a subtitle. */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_VoiceLine);

	/** Direct subtitle-show request channel (payload = FLoc_SubtitleLine). */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SubtitleShow);

	/** Direct subtitle-clear request channel (empty payload clears all; tag payload clears by speaker). */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SubtitleClear);

	/** Notification channel the subtitle subsystem BROADCASTS on when the visible set changes. */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SubtitleChanged);

	// --- Canonical TTS routing categories (passed to ISeam_TextToSpeech::Speak) ---

	/** TTS category for spoken subtitle/narration lines. */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TTS_Subtitle);

	// --- Canonical subtitle priority anchors (designers extend the DP.Loc.Subtitle.Priority root) ---

	/** Subtitle priority: ambient / background chatter (lowest, evicted first). */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SubtitlePriority_Ambient);

	/** Subtitle priority: standard dialogue. */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SubtitlePriority_Dialogue);

	/** Subtitle priority: critical / story-gating line (shown over everything else). */
	DESIGNPATTERNSLOCALIZATION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SubtitlePriority_Critical);
}
