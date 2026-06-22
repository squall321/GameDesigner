// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Cam_DeveloperSettings.generated.h"

class UCam_CameraMode;
class UCurveFloat;
class UCam_CameraShakeLibrary;
class UCam_PostProcessProfile;

/**
 * Maps a camera-mode identity tag to the mode class instanced when that tag is pushed. Authored in
 * project settings so designers add new modes without code, and so different projects can rebind the
 * shipped tags (Cam.Mode.ThirdPerson, …) to their own tuned mode subclasses.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ModeMapping
{
	GENERATED_BODY()

	/** The identity tag callers push (e.g. Cam.Mode.ThirdPerson). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (Categories = "Cam.Mode"))
	FGameplayTag ModeTag;

	/** The mode class to instance for this tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowAbstract = "false"))
	TSoftClassPtr<UCam_CameraMode> ModeClass;
};

/**
 * Project-wide configuration for the DesignPatternsCamera module. Appears under
 * Project Settings -> Plugins -> Design Patterns Camera. Edited with no code.
 *
 * Provides the tag->class mode registry, the default mode pushed when a director starts, and the
 * blend-shaping curve the stack uses. All gameplay-affecting numbers (arm length, FOV, lag, …) live
 * on the mode classes themselves; this asset only wires identity to behaviour.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Camera"))
class DESIGNPATTERNSCAMERA_API UCam_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCam_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. Never null in a configured project; null-checked by callers. */
	static const UCam_DeveloperSettings* Get();

	/** Resolve the mode class registered for a tag (loads the soft class). Null if unmapped. */
	TSubclassOf<UCam_CameraMode> ResolveModeClass(FGameplayTag ModeTag) const;

	/**
	 * Registry of mode tag -> mode class. A director pushing a tag instances the mapped class.
	 * Ship sensible defaults wiring the five built-in tags to the five built-in modes.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Modes")
	TArray<FCam_ModeMapping> ModeMappings;

	/**
	 * The mode tag a director pushes at base priority when it activates with an empty stack, so the
	 * camera always has a sensible default behaviour. Empty = director starts idle (uses live camera).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Modes", meta = (Categories = "Cam.Mode"))
	FGameplayTag DefaultModeTag;

	/**
	 * Priority assigned to the auto-pushed default mode. Kept low so any explicit push wins.
	 * Tunable, not magic: exposed so projects can layer their own base modes beneath it if needed.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Modes")
	int32 DefaultModePriority = 0;

	/**
	 * Optional blend-shaping curve mapping linear blend alpha [0,1] -> shaped alpha [0,1]. Null =
	 * the stack's built-in smoothstep ease.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Blend")
	TSoftObjectPtr<UCurveFloat> BlendCurve;

	/** When true the director registers its ICam_CameraModeProvider into the service locator on begin play. */
	UPROPERTY(EditAnywhere, Config, Category = "Integration")
	bool bRegisterAsService = true;

	// ---- Shake (shake library area) ----

	/**
	 * Default shake library a UCam_ShakeRequestComponent uses when it has no explicit override.
	 * Soft so the setting does not force-load the library into every cook that includes this module.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Shake")
	TSoftObjectPtr<UCam_CameraShakeLibrary> DefaultShakeLibrary;

	// ---- Targeting / lock-on (targeting area) ----

	/**
	 * Fallback maximum range (cm) a targeting component considers candidates within. Used only when a
	 * component leaves its own MaxTargetRange at the <= 0 "use project default" sentinel. Defensive
	 * default keeps targeting working before a project tunes it.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultMaxTargetRange = 2000.f;

	/**
	 * Fallback half-angle (deg) of the acquisition cone in front of the view. Used only when a
	 * component leaves its own AcquisitionHalfAngleDeg at the <= 0 sentinel.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float DefaultAcquisitionHalfAngleDeg = 60.f;

	/**
	 * Fallback sphere radius (cm) of the candidate overlap query. Used only when a component leaves
	 * its own CandidateOverlapRadius at the <= 0 sentinel.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultCandidateOverlapRadius = 2000.f;

	// ---- Additive deepening: post-process / photo mode (mechanism wiring only) ----

	/**
	 * Default post-process profile a director uses when it has no explicit PostProcessProfileOverride.
	 * Soft so the setting does not force-load the profile into every cook that includes this module.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "PostProcess")
	TSoftObjectPtr<UCam_PostProcessProfile> DefaultPostProcessProfile;

	/**
	 * Priority a photo-mode component pushes its free-fly camera mode at when it leaves its own
	 * PhotoModePriority at the <= 0 sentinel. High so photo mode wins over gameplay modes.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Photo", meta = (ClampMin = "0"))
	int32 DefaultPhotoModePriority = 1000;
};
