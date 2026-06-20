// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Audio runtime module for the DesignPatterns plugin.
 *
 * Genre-agnostic, COSMETIC/LOCAL audio framework. It WRAPS the engine's audio stack
 * (UGameplayStatics::PlaySound2D / SpawnSoundAtLocation, UAudioComponent, the AudioMixer
 * submix/bus API) behind a clean, data-driven, tag-keyed facade:
 *   - IAudio_AudioController : the "play by tag" seam other systems resolve via the service
 *     locator (DP.Service.Audio) and never depend on a concrete audio class for.
 *   - UAudio_SoundManagerSubsystem : a GameInstance subsystem that implements the controller,
 *     resolves sounds out of data-driven sound banks (async load), and enforces per-category
 *     concurrency limits, virtualization (oldest-steal), category volumes and ducking.
 *   - UAudio_MixController + mix-profile data assets : push/pop submix/bus volume snapshots by
 *     priority over the AudioMixer API.
 *
 * Audio is purely cosmetic and is NEVER replicated: it is driven by already-replicated gameplay
 * via the message bus (DP.Bus.* channels) and OnRep handlers on the gameplay side. The module
 * depends only on the core "DesignPatterns" module and the shared "DesignPatternsSeams" contracts;
 * it never hard-includes another Wave-1 / genre / sibling module.
 */
class FDesignPatternsAudioModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsAudio module.
 *
 * These are ROOT/anchor tags plus the small set of stable leaf keys this module itself needs
 * (its service-locator key and its bus channels). Concrete SOUND ids and BUS CATEGORY leaves are
 * authored by the game project as CHILD tags under the roots below (in the project's tag config or
 * its own native tags); anchoring the roots here guarantees the hierarchy exists at startup so
 * tag-hierarchy matching always works.
 *
 * Tag layout:
 *   DP.Audio                  - module root (umbrella for everything below)
 *   DP.Audio.Sound.*          - per-sound identity keys (resolved from a sound bank). PROJECT-AUTHORED.
 *   DP.Audio.Category         - bus/category root. A "category" groups voices for concurrency limiting,
 *                               category-volume control and ducking. PROJECT-AUTHORED leaves, e.g.:
 *                               DP.Audio.Category.SFX / UI / Music / Ambience / Voice / Footsteps.
 *   DP.Audio.Mix.*            - mix-profile identity keys (resolved from a mix-profile data asset). PROJECT-AUTHORED.
 *   DP.Service.Audio          - service-locator key under which the sound manager registers itself.
 *   DP.Bus.Audio.*            - message-bus channels this module listens to / can broadcast on.
 */
namespace AudioNativeTags
{
	/** Module umbrella root: DP.Audio. Anchors the whole audio tag hierarchy. */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Audio);

	/** Root for per-sound identity keys (DP.Audio.Sound.UI.Click ...). PROJECT authors the leaves. */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Sound);

	/**
	 * Root for bus/category keys (DP.Audio.Category.SFX / UI / Music ...). A category is the unit of
	 * concurrency limiting, category-volume scaling and ducking. PROJECT authors the leaves.
	 */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Category);

	/** Root for mix-profile identity keys (DP.Audio.Mix.Combat / Pause ...). PROJECT authors the leaves. */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mix);

	/** Service-locator key the sound manager registers itself under (child of DP.Service). */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Audio);

	/** Root for message-bus channels this module participates in (children of DP.Bus). */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	/**
	 * Bus channel a game broadcasts to request a one-shot play-by-tag without resolving the
	 * controller directly (payload: FAudio_PlayRequest). The sound manager listens on this.
	 */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Play);

	/**
	 * Bus channel a game broadcasts to push/pop a named mix profile by priority
	 * (payload: FAudio_MixRequest). The mix controller listens on this.
	 */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Mix);

	/**
	 * Bus channel a game broadcasts to set a category volume multiplier at runtime
	 * (payload: FAudio_CategoryVolumeRequest). The sound manager listens on this.
	 */
	DESIGNPATTERNSAUDIO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_CategoryVolume);
}
