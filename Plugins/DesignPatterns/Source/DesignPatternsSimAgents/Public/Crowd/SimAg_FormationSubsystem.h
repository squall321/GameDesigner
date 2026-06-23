// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_FormationSubsystem.generated.h"

class USimAg_FormationAsset;

/**
 * World-scoped formation slot resolver. Given a group id, a formation pattern (USimAg_FormationAsset by
 * tag), and an anchor transform, it returns the world stand-position for a member's slot. It also assigns
 * stable slot indices per (group, agent) so members keep their place.
 *
 * Transient query state only — NEVER replicated. Group MEMBERSHIP lives on the replicated
 * USimAg_GroupComponent; this subsystem only computes offsets and hands out slot indices, so it can be
 * rebuilt freely on any machine. Wraps no engine system; pure math over the formation asset.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_FormationSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Resolve the world stand-position for SlotIndex of a formation, given an anchor (the leader's goal /
	 * transform). FormationTag selects the pattern; an invalid/unknown tag uses the procedural grid.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FVector ResolveSlotWorld(FGameplayTag FormationTag, int32 SlotIndex, const FVector& Anchor, const FRotator& AnchorRotation) const;

	/**
	 * Get (assigning if necessary) the stable slot index for Agent within Group. Slot 0 is reserved for the
	 * group leader; followers get 1..N in assignment order. Idempotent per (group, agent).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	int32 AssignSlot(FSeam_EntityId Group, FSeam_EntityId Agent);

	/** Release Agent's slot in Group (e.g. on leave/death) so it can be reused. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void ReleaseSlot(FSeam_EntityId Group, FSeam_EntityId Agent);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Per-group ordered membership (assignment order == slot index). Transient query state only. */
	TMap<FSeam_EntityId, TArray<FSeam_EntityId>> GroupMembers;

	/** Cached default slot spacing (world units) from settings. */
	float DefaultSpacing = 150.f;

	/** Resolve a formation asset by tag via the core data registry (cached by the registry). Null-safe. */
	USimAg_FormationAsset* ResolveFormation(const FGameplayTag& FormationTag) const;
};
