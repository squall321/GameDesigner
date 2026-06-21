// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_TacticalBusPayloads.generated.h"

/**
 * Flat, weak-ref-free payload broadcast on DP.Bus.AI.Cover.Claimed when an agent claims or releases a
 * cover point. Holds only net/save-safe value types so it is safe to flatten into the bus's
 * FInstancedStruct and queue for deferred dispatch — exactly like the existing AI wave/squad payloads.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_CoverClaimPayload
{
	GENERATED_BODY()

	/** Stable id of the agent claiming/releasing the cover. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	FSeam_EntityId AgentId;

	/** Stable id of the affected cover point. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	FSeam_EntityId CoverId;

	/** Cover type/classification tag (e.g. AI.Cover.Full). May be empty. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	FGameplayTag CoverTypeTag;

	/** True when the cover was just CLAIMED; false when it was RELEASED. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	bool bClaimed = false;

	/** World location of the cover stand point (for cosmetic UI / debug). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	FVector CoverLocation = FVector::ZeroVector;

	FAI_CoverClaimPayload() = default;
};

/**
 * Flat payload broadcast on DP.Bus.AI.Tactic when a member begins / changes a group tactic. Lets UI,
 * audio and analytics observe squad coordination without depending on the AI tactics component.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_TacticPayload
{
	GENERATED_BODY()

	/** Stable id of the agent executing the tactic. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Tactic")
	FSeam_EntityId AgentId;

	/** Squad the agent belongs to (invalid if solo). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Tactic")
	FGuid SquadId;

	/** The tactic being executed (e.g. AI.Tactic.Advance). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Tactic")
	FGameplayTag TacticTag;

	FAI_TacticPayload() = default;
};
