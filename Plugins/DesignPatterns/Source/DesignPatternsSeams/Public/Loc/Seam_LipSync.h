// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_LipSync.generated.h"

class USoundBase;

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_LipSync : public UInterface
{
	GENERATED_BODY()
};

/**
 * PROJECT-BRIDGE seam onto a facial / lip-sync animation backend.
 *
 * Lets the Localization voice subsystem drive viseme animation as it plays a per-culture VO clip WITHOUT
 * a hard dependency on any animation/character module. A thin project-side adapter (an anim component, a
 * MetaHuman driver, or a middleware bridge) implements this seam and self-registers a
 * TScriptInterface<ISeam_LipSync> under a service-locator key owned by the Localization module
 * (DP.Service.Loc.LipSync). The producer resolves it weakly and re-resolves on use, so a project with no
 * facial-animation backend simply gets silent no-ops (the shipped INERT default below).
 *
 * Engine-only types cross this seam (FGameplayTag speaker identity, USoundBase* clip, a UObject* curve
 * asset the adapter may interpret as an authored viseme/animation curve). No Localization-module type
 * leaks across the seam, and no Slate type is referenced, so this header keeps the Seams module a leaf
 * (Core/CoreUObject/GameplayTags only).
 *
 * House style: BlueprintNativeEvent UINTERFACE (project-bridge seam, like ISeam_EncounterDirector and
 * ISeam_TextToSpeech) so it is resolvable as a TScriptInterface and a project may implement it in
 * Blueprint. All three methods have inert *_Implementation defaults in the .cpp.
 *
 * THREADING / AUTHORITY: lip-sync is purely cosmetic, player-local presentation — there is no authority
 * concern and nothing replicates. Calls happen on the game thread from the voice subsystem.
 */
class DESIGNPATTERNSSEAMS_API ISeam_LipSync
{
	GENERATED_BODY()

public:
	/**
	 * Begin a lip-sync pass for Speaker driven by the playing VO clip. The adapter typically resolves the
	 * speaking character from Speaker, starts a phoneme/viseme analysis of Vo (or reads CurveAsset if the
	 * project authored a baked viseme curve), and begins animating. Calling Begin again for the same
	 * Speaker supersedes any in-flight pass for that speaker.
	 *
	 * @param Speaker    Designer speaker tag (e.g. DP.Loc.Speaker.Narrator); routing identity only.
	 * @param Vo         The voice clip being played. May be null (adapter falls back to CurveAsset/text).
	 * @param CurveAsset Optional authored viseme/animation curve asset the adapter may interpret. May be null.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|LipSync")
	void BeginLipSync(FGameplayTag Speaker, USoundBase* Vo, UObject* CurveAsset);

	/**
	 * Push a single viseme sample for Speaker at a normalized clip time. Lets a producer that performs its
	 * own phoneme extraction stream visemes to the adapter, rather than relying on the adapter to analyze
	 * the clip itself. A no-op for adapters that drive their own analysis.
	 *
	 * @param Speaker Designer speaker tag the sample belongs to.
	 * @param Time    Clip time in seconds (>= 0) the viseme applies at.
	 * @param Viseme  Opaque viseme index (a small enum agreed between producer and adapter, e.g. 0..14).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|LipSync")
	void PushViseme(FGameplayTag Speaker, float Time, uint8 Viseme);

	/**
	 * End the lip-sync pass for Speaker (clip finished, was stopped, or the line was cleared). The adapter
	 * blends the mouth back to rest. Safe to call for a speaker with no active pass (no-op).
	 *
	 * @param Speaker Designer speaker tag whose pass should end.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|LipSync")
	void EndLipSync(FGameplayTag Speaker);
};
