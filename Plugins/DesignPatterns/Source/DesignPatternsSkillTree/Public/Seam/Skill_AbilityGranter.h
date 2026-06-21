// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Skill_AbilityGranter.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USkill_AbilityGranter : public UInterface
{
	GENERATED_BODY()
};

/**
 * Module-private WRITE seam that translates "a skill node was learned/refunded" into a concrete ability
 * grant/revoke on the owner, WITHOUT this module depending on any specific ability backend.
 *
 * A project ships a thin adapter implementing this against whatever it uses — typically the core
 * UDP_GameplayActionComponent (GrantAction/RemoveAction by tag), but equally GAS, a bespoke buff system,
 * or nothing. The progression area resolves the implementation from the owning pawn (Implements<> /
 * GetComponentByInterface) or from the service locator under SkillNativeTags::Service_Skill_AbilityGranter.
 * When no implementation resolves, ability grants degrade to an inert no-op (the rank is still recorded
 * and its passive GrantedModifierTags still apply) — a documented inert default, never a crash.
 *
 * AUTHORITY: GrantAbility/RevokeAbility mutate gameplay state and are server-only. The progression
 * component invokes them only after an authority check, but implementations MUST also defensively guard
 * HasAuthority() (or equivalent) before mutating, per the seam contract. HasAbility is a read and may run
 * anywhere.
 */
class DESIGNPATTERNSSKILLTREE_API ISkill_AbilityGranter
{
	GENERATED_BODY()

public:
	/**
	 * Grant (or refresh to) Rank of the ability identified by AbilityTag, attributing it to SourceTag (the
	 * learning skill node's DataTag) so it can be revoked precisely on respec. Returns true if the ability
	 * is now granted at the requested rank. AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Skill|Seam")
	bool GrantAbility(FGameplayTag AbilityTag, int32 Rank, FGameplayTag SourceTag);

	/**
	 * Revoke the ability AbilityTag that was previously granted by SourceTag (the skill node being
	 * refunded). Returns true if an ability was removed. Implementations should only remove grants whose
	 * source matches, so two nodes granting the same ability tag don't clobber each other. AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Skill|Seam")
	bool RevokeAbility(FGameplayTag AbilityTag, FGameplayTag SourceTag);

	/** True if the owner currently has the ability AbilityTag granted (from any source). Read-only. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Skill|Seam")
	bool HasAbility(FGameplayTag AbilityTag) const;
};
