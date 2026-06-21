// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Skill-tree / talent-graph runtime module for the DesignPatterns plugin.
 *
 * This module layers a generic, genre-neutral talent/skill graph on top of the core
 * "DesignPatterns" module (UDP_DataAsset + data registry, message bus, service locator, save
 * subsystem, gameplay-action component) and the shared "DesignPatternsSeams" contracts
 * (ISeam_Wallet, ISeam_PurchaseTarget, ISeam_Persistable). It owns:
 *   - this area: the authored content vocabulary (USkill_SkillDefinition / USkill_SkillTreeDefinition
 *     data assets), the value/result types (Skill_Types.h), the two module-private seams
 *     (ISkill_AbilityGranter, ISkill_PointSource), the developer settings, and the native anchor tags;
 *   - sibling areas (authored elsewhere in this module): the replicated per-pawn progression component,
 *     the learn/respec command flow, the wallet/action adapters, and the save participant.
 *
 * COUPLING: nothing here hard-includes a genre or sibling Wave-3 module's concrete header. Spending
 * skill points debits a currency through the shared ISeam_Wallet/ISeam_PurchaseTarget seams; granting
 * the actual ability is delegated through this module's own ISkill_AbilityGranter seam (a project
 * adapter wires it to its ability backend, e.g. the core UDP_GameplayActionComponent). Earned-point
 * budgets and owner level are read through ISkill_PointSource. Save/load wraps the core save subsystem
 * via ISeam_Persistable. Unresolved seams degrade to a documented inert default (no grant, zero points).
 *
 * The native tags below are ROOT/anchor tags only, so the hierarchy exists at startup; concrete leaf
 * skill ids, ability ids, currencies and point channels are authored by the GAME project as child tags.
 */
class FDesignPatternsSkillTreeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsSkillTree module.
 *
 * Only roots/anchors are declared so message-bus hierarchy matching and service-locator lookups have
 * guaranteed parents at startup. Leaf vocabulary (specific skills, abilities, currencies, channels) is
 * authored by the project — this module never bakes gameplay-specific leaf tags.
 *
 * Channel layout (all under the core DP.Bus root, so a listener on `DP.Bus` still receives them):
 *   DP.Bus.Skill.Learned   — a rank in a skill was learned   (payload authored by the progression area).
 *   DP.Bus.Skill.Unlearned — a rank was refunded during respec.
 *   DP.Bus.Skill.Respec    — a full respec completed.
 *   DP.Bus.Skill.PointsChanged — available/spent point totals changed.
 *
 * Service-locator key anchors (children of the core DP.Service root):
 *   DP.Service.Skill.AbilityGranter — a project adapter registers its ISkill_AbilityGranter here.
 *   DP.Service.Skill.PointSource    — a project adapter registers its ISkill_PointSource here.
 */
namespace SkillNativeTags
{
	// ---- Vocabulary roots (the authored content namespace) ----

	/** Root of the entire skill-tree vocabulary: Skill.* */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill);

	/** Root under which concrete skill-node identity tags are authored (Skill.Node.*). */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Node);

	/** Root under which granted-ability identity tags are authored (Skill.Ability.*). */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Ability);

	/** Root under which point-earning channels are authored (Skill.Channel.*), e.g. Skill.Channel.Main. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Channel);

	/** Root under which mutually-exclusive skill groups are authored (Skill.Mutex.*). */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Mutex);

	/** Persistence-kind tag this module reports through ISeam_Persistable (Skill.Persist). */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Skill_Persist);

	// ---- Message-bus channel anchors (children of the core DP.Bus root) ----

	/** Root of every skill bus channel: DP.Bus.Skill.* */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Skill);

	/** A rank in a skill node was successfully learned. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Skill_Learned);

	/** A previously-learned rank was refunded (respec / undo). */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Skill_Unlearned);

	/** A full respec of a tree completed. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Skill_Respec);

	/** Available / spent point totals changed. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Skill_PointsChanged);

	// ---- Service-locator key anchors (children of the core DP.Service root) ----

	/** Key a project adapter registers its ISkill_AbilityGranter implementation under. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Skill_AbilityGranter);

	/** Key a project adapter registers its ISkill_PointSource implementation under. */
	DESIGNPATTERNSSKILLTREE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Skill_PointSource);
}
