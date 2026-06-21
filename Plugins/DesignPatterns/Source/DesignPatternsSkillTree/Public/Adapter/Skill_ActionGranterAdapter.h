// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/Skill_AbilityGranter.h"
#include "Action/DPGameplayActionLite.h"
#include "Action/DPGameplayActionInterface.h"
#include "Skill_ActionGranterAdapter.generated.h"

class UDP_GameplayActionComponent;

/**
 * Bookkeeping for one ability granted through this adapter: the action component handle it produced,
 * keyed implicitly by the (AbilityTag, SourceTag) pair stored alongside. Lets RevokeAbility find and
 * remove exactly the grant a given skill produced, even when several skills grant the same ability tag.
 */
USTRUCT()
struct DESIGNPATTERNSSKILLTREE_API FSkill_GrantedAbilityRecord
{
	GENERATED_BODY()

	/** The ability id that was granted (key part 1). */
	UPROPERTY()
	FGameplayTag AbilityTag;

	/** The skill that requested the grant (key part 2), so two skills granting the same ability are distinct. */
	UPROPERTY()
	FGameplayTag SourceTag;

	/** The rank this grant currently reflects, so a re-grant at the same rank is a cheap no-op. */
	UPROPERTY()
	int32 GrantedRank = 0;

	/** The handle returned by UDP_GameplayActionComponent::GrantAction, used to RemoveAction on revoke. */
	UPROPERTY()
	FDP_ActionSpecHandle Handle;

	FSkill_GrantedAbilityRecord() = default;
};

/**
 * Skill -> ability adapter built on the lightweight action system (UDP_GameplayActionComponent).
 *
 * Implements the ISkill_AbilityGranter seam so USkill_SkillComponent can grant a skill's linked ability
 * without hard-knowing how abilities are implemented. A designer maps each ability tag to a soft action
 * class via AbilityClassMap; on GrantAbility this adapter sync-loads the class and forwards to the owner's
 * UDP_GameplayActionComponent::GrantAction (resolved off the owning actor), recording the resulting handle
 * per (AbilityTag, SourceTag). RevokeAbility removes that exact grant via RemoveAction.
 *
 * This is a thin authority-side adapter — it holds no replicated state of its own; the action component it
 * forwards to owns the (server-authoritative) grant state. Place it on the same actor as the skill component
 * and the action component.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSKILLTREE_API USkill_ActionGranterAdapter : public UActorComponent, public ISkill_AbilityGranter
{
	GENERATED_BODY()

public:
	USkill_ActionGranterAdapter();

	//~ Begin ISkill_AbilityGranter
	virtual bool GrantAbility_Implementation(FGameplayTag AbilityTag, int32 Rank, FGameplayTag SourceTag) override;
	virtual bool RevokeAbility_Implementation(FGameplayTag AbilityTag, FGameplayTag SourceTag) override;
	virtual bool HasAbility_Implementation(FGameplayTag AbilityTag) const override;
	//~ End ISkill_AbilityGranter

	/**
	 * Designer map: ability id -> the lightweight action class to grant for it. Soft so the action assets
	 * are only loaded when a skill that grants the ability is actually learned. An unmapped ability tag
	 * means "no concrete action" — GrantAbility logs and returns false (the skill learn still succeeds; the
	 * component treats an absent/false grant as a skipped-grant degrade).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkillTree|Granter")
	TMap<FGameplayTag, TSoftClassPtr<UDP_GameplayActionLite>> AbilityClassMap;

private:
	/** Live records of abilities this adapter has granted, so revokes target the exact action-component handle. */
	UPROPERTY()
	TArray<FSkill_GrantedAbilityRecord> GrantedRecords;

	/** Resolve the owner's UDP_GameplayActionComponent (the backend we forward grants to). May be null. */
	UDP_GameplayActionComponent* ResolveActionComponent() const;

	/** Find a granted record by (AbilityTag, SourceTag), or null. */
	FSkill_GrantedAbilityRecord* FindRecord(const FGameplayTag& AbilityTag, const FGameplayTag& SourceTag);

	/** True on the network authority; grant/revoke mutate the (server-authoritative) action component. */
	bool HasAuthorityToMutate() const;
};
