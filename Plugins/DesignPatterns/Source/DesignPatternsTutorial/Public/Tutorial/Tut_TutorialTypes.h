// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "Tut_TutorialTypes.generated.h"

/**
 * Runtime status of a tutorial run, surfaced to the ViewModel and carried in tutorial bus events.
 */
UENUM(BlueprintType)
enum class ETut_TutorialStatus : uint8
{
	/** No tutorial is currently running. */
	Inactive,

	/** A tutorial is running and is waiting for the current step's completion condition. */
	Running,

	/** The active step's trigger condition has not yet fired (waiting to surface the step). */
	WaitingForTrigger,

	/** The tutorial finished all steps (or was skipped) this session. */
	Completed
};

/**
 * Bus payload broadcast by the tutorial subsystem on DP.Bus.Tutorial.* channels.
 *
 * Carries only value-typed, already-local data (tags + indices + text) so any HUD/UI can react to tutorial
 * progress without a hard dependency on this module. NOT replicated (tutorials are local/cosmetic).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSTUTORIAL_API FTut_TutorialEvent
{
	GENERATED_BODY()

	/** The DataTag identity of the tutorial this event refers to. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	FGameplayTag TutorialTag;

	/** The new status of the tutorial at the time of the event. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	ETut_TutorialStatus Status = ETut_TutorialStatus::Inactive;

	/** Zero-based index of the active step (INDEX_NONE when none / completed). */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	int32 StepIndex = INDEX_NONE;

	/** Total number of steps in the tutorial. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	int32 StepCount = 0;

	/** The current step's instruction text (empty when none). */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial")
	FText Instruction;

	FTut_TutorialEvent() = default;
};

/**
 * Bus payload broadcast by the hint subsystem on DP.Bus.Tutorial.HintShown when a contextual hint surfaces.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSTUTORIAL_API FTut_HintEvent
{
	GENERATED_BODY()

	/** The DataTag identity of the hint definition that fired. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FGameplayTag HintTag;

	/** The hint body text that was shown. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FText Text;

	/** The priority the hint was queued at (higher pre-empts lower). */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	int32 Priority = 0;

	FTut_HintEvent() = default;
};

/**
 * Notification-request payload the hint subsystem broadcasts on the shared HUD notify channel
 * (DP.Bus.HUD.Notify) so the HUD module surfaces a hint as a toast — WITHOUT this module including the HUD
 * module's concrete payload type.
 *
 * The HUD module's notification map (data-driven on the HUD side) adapts this request onto its own
 * FHUD_Notification, so the two modules share only the bus channel TAG, never a header. If no HUD listens the
 * broadcast is an inert no-op (the hint still emits FTut_HintEvent for telemetry). The fields mirror the
 * generic notification shape (category / title / body / duration / priority / dedupe key) so the adapter is a
 * trivial field copy.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSTUTORIAL_API FTut_HintNotifyRequest
{
	GENERATED_BODY()

	/** Styling/classification category (child of DP.HUD.Notify) for the toast. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint", meta = (Categories = "DP.HUD.Notify"))
	FGameplayTag Category;

	/** Toast headline (empty for hints, which are body-only). */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FText Title;

	/** The hint text shown as the toast body. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	FText Body;

	/** Seconds the toast stays on screen (<= 0 means the HUD's default / sticky). */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	float Duration = 0.f;

	/** Relative importance, mirrored from the hint's priority. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint")
	int32 Priority = 0;

	/** De-dupe key so the same hint replaces rather than stacks; mirrors the hint DataTag. */
	UPROPERTY(BlueprintReadOnly, Category = "Tutorial|Hint", meta = (Categories = "DP"))
	FGameplayTag DedupeKey;

	FTut_HintNotifyRequest() = default;
};

/**
 * Durable record persisted by UTut_TutorialSubsystem through ISeam_Persistable.
 *
 * Holds the set of completed tutorial DataTags so that, after a load, completed tutorials never replay. This
 * is the FInstancedStruct payload type captured in CaptureState and applied in RestoreState — it is a SAVE
 * record only (never a replicated UPROPERTY), so it carries the FGameplayTagContainer directly.
 */
USTRUCT()
struct DESIGNPATTERNSTUTORIAL_API FTut_TutorialSaveRecord
{
	GENERATED_BODY()

	/** Every tutorial DataTag the player has completed (or explicitly skipped, which also suppresses replay). */
	UPROPERTY(SaveGame)
	FGameplayTagContainer CompletedTutorials;

	FTut_TutorialSaveRecord() = default;
};
