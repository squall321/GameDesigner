// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5. Used here only
// inside save-side / message-bus payload structs (never as a plain Replicated UPROPERTY).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "SimAg_JobTypes.generated.h"

/**
 * Lifecycle phase of a single job-board posting. Drives which agents may see/claim it and how the
 * board prunes completed work. Authoritative on the server; replicated to clients as a uint8 on the
 * job entry so observers can grey out claimed/finished postings.
 */
UENUM(BlueprintType)
enum class ESimAg_JobState : uint8
{
	/** Posted and available for any qualified agent to claim. */
	Open,
	/** Claimed by an agent and being worked. Not offered to other claimants. */
	Claimed,
	/** Finished successfully; eligible for board pruning. */
	Completed,
	/** Abandoned / timed out / cancelled; eligible for board pruning. */
	Cancelled
};

/**
 * Designer-facing intent to post one unit of work onto the job board. This is the INPUT to
 * ISimAg_JobProvider::PostJob — it carries everything needed to create a posting but holds no runtime
 * identity (the board assigns the FGuid). Genre-neutral: the kind of work and any qualification are
 * gameplay tags, so a colony "haul" job and a town "guard patrol" job share one machinery.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobRequest
{
	GENERATED_BODY()

	/** What kind of work this is (child of a project's job-kind tag root). Used for relevance matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag JobKind;

	/**
	 * Optional capability/skill an agent must advertise to be eligible. Empty means "anyone may claim".
	 * Matched by tag hierarchy, so a job requiring "Job.Skill.Crafting" accepts an agent who has
	 * "Job.Skill.Crafting.Smithing".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag RequiredSkill;

	/** Where the work happens, in world space. Drives distance-based relevance scoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FVector WorldLocation = FVector::ZeroVector;

	/**
	 * Relative importance, designer-authored. Higher posts are preferred when several are otherwise
	 * comparable. Not a hardcoded weight in code — the brain combines it with distance via a curve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs", meta = (ClampMin = "0.0"))
	float Priority = 1.f;

	/** Stable id of the entity that posted the work (a building, faction, or player). May be invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FSeam_EntityId Poster;

	FSimAg_JobRequest() = default;
};

/**
 * A lightweight, value-typed handle to a posting returned from a query/claim. Distinct from the
 * replicated FSimAg_JobEntry: this is a copyable snapshot a brain can stash on the blackboard or pass
 * around without holding the live array element. An invalid JobId means "no job".
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobHandle
{
	GENERATED_BODY()

	/** Unique id of the posting (assigned by the board at PostJob time). Invalid = no job. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGuid JobId;

	/** Kind of work (copied from the posting), so a holder can branch without re-querying the board. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGameplayTag JobKind;

	/** Where the work happens — what the brain writes as its move target. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FVector WorldLocation = FVector::ZeroVector;

	/** Current lifecycle phase at the time the handle was produced. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	ESimAg_JobState State = ESimAg_JobState::Open;

	FSimAg_JobHandle() = default;

	/** True when this handle refers to a real posting. */
	bool IsValid() const { return JobId.IsValid(); }

	/** The empty/invalid handle. */
	static FSimAg_JobHandle Invalid() { return FSimAg_JobHandle(); }

	FString ToString() const
	{
		return FString::Printf(TEXT("Job[%s kind=%s @%s state=%d]"),
			*JobId.ToString(EGuidFormats::Short), *JobKind.ToString(), *WorldLocation.ToCompactString(),
			static_cast<int32>(State));
	}
};

/**
 * Message-bus payload broadcast when a job posting changes state (posted / claimed / completed /
 * cancelled). Carried as an FInstancedStruct through UDP_MessageBusSubsystem so UI and other systems
 * can react without polling the board.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobEvent
{
	GENERATED_BODY()

	/** The posting that changed. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGuid JobId;

	/** Kind of work involved. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag JobKind;

	/** New lifecycle phase after the change. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Jobs")
	ESimAg_JobState NewState = ESimAg_JobState::Open;

	/** Stable id of the agent that now holds the job (invalid when open / unclaimed). */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Jobs")
	FSeam_EntityId Claimant;
};
