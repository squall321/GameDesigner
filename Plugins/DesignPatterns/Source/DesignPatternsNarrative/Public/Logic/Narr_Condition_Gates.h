// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logic/Narr_Condition.h"   // shared UNarr_Condition base (this header only ADDS leaves)
#include "Narr_Condition_Gates.generated.h"

class ISeam_Reputation;
class ISeam_Wallet;

/**
 * Net-new dialogue/quest condition leaves that extend the shared UNarr_Condition mini-language with gates
 * the shipped set lacks: reputation standing, currency possession, item possession, and a hub-counter
 * skill check.
 *
 * Each leaf resolves its OWN seam from the service locator via the Source's world context — EXACTLY as the
 * shipped UNarr_Condition_TimeOfDay resolves ISeam_SimClock — so no method is added to the frozen
 * INarr_StoryConditionSource and these gates carry no concrete-subsystem dependency. All fail closed when
 * their backend/source is unavailable (subject to bDefaultWhenNoSource / bInvert via Finalize()).
 */

/**
 * Leaf: passes when the player meets MinStanding with FactionOrNpcTag, read through the shared
 * ISeam_Reputation seam (resolved under DP.Service.Narrative.Reputation). An ABSENT provider fails closed
 * (HasReputation is explicit so "no reputation system" is distinguishable from "neutral").
 */
UCLASS(meta = (DisplayName = "Reputation Standing"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_Reputation : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The faction or NPC tag whose standing is tested. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	FGameplayTag FactionOrNpcTag;

	/** Minimum standing the player must meet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	int32 MinStanding = 0;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/**
 * Leaf: passes when the player holds at least Amount of CurrencyTag, read through the existing read-only
 * ISeam_Wallet seam (resolved under DP.Service.Economy.Wallet by convention, or off the conversing actor).
 * Used for "pay the toll" / "bribe" dialogue choices. Absent wallet fails closed.
 */
UCLASS(meta = (DisplayName = "Has Currency"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_HasCurrency : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The currency to test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	FGameplayTag CurrencyTag;

	/** Minimum amount required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (ClampMin = "1"))
	int64 Amount = 1;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/**
 * Leaf: passes when the player holds at least RequiredAmount of ItemTag, read through the shared read-only
 * ISeam_ItemQuery seam (resolved off the conversing actor). Used for "show the artifact" dialogue choices.
 * Absent inventory fails closed.
 */
UCLASS(meta = (DisplayName = "Has Item"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_HasItem : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The item to test. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	FGameplayTag ItemTag;

	/** Minimum amount required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (ClampMin = "1"))
	int32 RequiredAmount = 1;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};

/**
 * Leaf: a skill check against a world-hub counter read through the FROZEN INarr_StoryConditionSource's
 * QueryCounter (which already exists), so no new seam is needed. SkillKey names the counter (e.g. a stat
 * stored as a hub counter); passes when its value >= Difficulty. This keeps skill checks inside the existing
 * source facade rather than reaching into a stats module.
 */
UCLASS(meta = (DisplayName = "Skill Check"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Condition_SkillCheck : public UNarr_Condition
{
	GENERATED_BODY()

public:
	/** The hub counter that holds the skill/stat value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition", meta = (Categories = "DP.WorldHub"))
	FGameplayTag SkillKey;

	/** The difficulty the counter must meet or exceed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Condition")
	int64 Difficulty = 1;

	virtual bool IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeCondition() const override;
};
