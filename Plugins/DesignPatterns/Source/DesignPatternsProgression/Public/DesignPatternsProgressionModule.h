// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Progression runtime module for the DesignPatterns plugin.
 *
 * Genre-agnostic player-progression layer built entirely from the framework's patterns:
 *   - UProg_WalletComponent       : a server-authoritative, replicated, tag-keyed currency purse that
 *                                   IMPLEMENTS the read-only ISeam_Wallet contract. Other modules
 *                                   (shop, quests, HUD, skill cost-checks) read balances through the
 *                                   seam without depending on this module; spending/granting is an
 *                                   authority-only mutation that lives on the component, never on the
 *                                   read-only seam.
 *   - UProg_Condition             : an EditInlineNew, Abstract "is this satisfied?" leaf (Strategy
 *                                   pattern). Ships flag / counter / bus-counter implementations.
 *   - UProg_AchievementDefinition : a UDP_DataAsset describing one achievement (id, name/desc, icon,
 *                                   conditions, optional currency reward).
 *   - UProg_AchievementSubsystem  : a GameInstance subsystem that tracks achievement progress, unlocks
 *                                   when every condition passes (re-evaluated on message-bus events),
 *                                   fires a bus event + an analytics event + an optional platform
 *                                   trophy on unlock, grants the configured reward through ISeam_Wallet,
 *                                   and persists its unlocked set through ISeam_Persistable.
 *
 * AUTHORITY & REPLICATION: replicated state lives only on the wallet UActorComponent. The subsystem
 * holds NO replicated state; it observes already-replicated/bus state and mutates its tracked
 * progress only on authority.
 *
 * The module depends only on the core "DesignPatterns" module and the shared "DesignPatternsSeams"
 * contracts. It never hard-includes another genre / sibling module; all cross-module coupling is via
 * seams (ISeam_Wallet, ISeam_AnalyticsSink, ISeam_PlatformAchievements, ISeam_Persistable) resolved
 * from the service locator / off the owning actor, plus the message bus. Unresolved seams degrade to a
 * documented inert default (no analytics, no platform trophy, no reward), never a crash.
 */
class FDesignPatternsProgressionModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsProgression module.
 *
 * These are ROOT / anchor tags plus the stable leaf keys this module itself needs (its service-locator
 * keys, its bus channels, and the persistence-kind key for the achievement save record). Concrete
 * CURRENCY ids (DP.Prog.Currency.Gold ...) and ACHIEVEMENT ids (DP.Prog.Achievement.FirstBlood ...) are
 * authored by the game project as CHILD tags; anchoring the roots here guarantees the hierarchy exists
 * at startup so tag-hierarchy matching always works.
 *
 * Tag layout:
 *   DP.Prog                          - module root (umbrella for everything below).
 *   DP.Prog.Currency.*               - per-currency identity keys. PROJECT-AUTHORED leaves.
 *   DP.Prog.Achievement.*            - per-achievement identity keys. PROJECT-AUTHORED leaves.
 *   DP.Prog.Persist.Achievements     - ISeam_Persistable kind tag for the achievement unlocked-set record.
 *   DP.Service.Wallet.Analytics      - service key the project's analytics sink registers under (resolved weakly).
 *   DP.Service.Prog.PlatformTrophies - service key the project's platform-achievement bridge registers under.
 *   DP.Bus.Prog                      - message-bus root for this module's channels.
 *   DP.Bus.Prog.BalanceChanged       - broadcast when a wallet balance changes (payload FProg_BalanceChangedEvent).
 *   DP.Bus.Prog.AchievementProgress  - broadcast when an achievement's progress fraction changes (payload FProg_AchievementEvent).
 *   DP.Bus.Prog.AchievementUnlocked  - broadcast when an achievement unlocks (payload FProg_AchievementEvent).
 */
namespace ProgTags
{
	/** Module umbrella root: DP.Prog. Anchors the whole progression tag hierarchy. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Prog);

	/** Root for per-currency identity keys (DP.Prog.Currency.Gold ...). PROJECT authors the leaves. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Currency);

	/** Root for per-achievement identity keys (DP.Prog.Achievement.FirstBlood ...). PROJECT authors the leaves. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Achievement);

	/** ISeam_Persistable kind tag identifying the achievement subsystem's unlocked-set save record. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Achievements);

	/** Service-locator key the project's analytics sink registers under; resolved WEAKLY by the achievement subsystem. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Analytics);

	/** Service-locator key the project's optional platform-achievement bridge registers under. */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_PlatformTrophies);

	/** Root for message-bus channels this module participates in (children of DP.Bus). */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	/** Bus channel broadcast when a wallet balance changes (payload FProg_BalanceChangedEvent). */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BalanceChanged);

	/** Bus channel broadcast when an achievement's progress fraction changes (payload FProg_AchievementEvent). */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AchievementProgress);

	/** Bus channel broadcast when an achievement unlocks (payload FProg_AchievementEvent). */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AchievementUnlocked);

	/**
	 * Bus channel the shop broadcasts a purchase OUTCOME on after TryPurchase resolves (payload
	 * FProg_ShopPurchaseEvent). HUD / analytics / SFX can react without binding to the vendor component.
	 */
	DESIGNPATTERNSPROGRESSION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_ShopPurchased);
}
