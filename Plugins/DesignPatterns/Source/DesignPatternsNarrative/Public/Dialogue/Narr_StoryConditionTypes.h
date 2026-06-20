// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
// The condition-source seam (INarr_StoryConditionSource) is owned by the dialogue area's Seam header;
// effects write through it, so we include it here and never redefine it.
#include "Seam/Narr_StoryConditionSource.h"
#include "Narr_StoryConditionTypes.generated.h"

/**
 * The shared EFFECT mini-language base + the world-state write leaves.
 *
 * This header is the SINGLE owner of UNarr_Effect (the inline, composable narrative effect base) and the
 * two authoritative write leaves (UNarr_Effect_SetFlag / UNarr_Effect_AddCounter). It is consumed by BOTH
 * narrative areas:
 *   - the story-director area (this area): a story beat's CompletionEffects are UNarr_Effect instances;
 *   - the dialogue area: dialogue nodes/choices fire UNarr_Effect instances, and Logic/Narr_Effect.h
 *     extends the set with a net-new observer-only bus-broadcast leaf.
 *
 * Effects never depend on a concrete subsystem or on DesignPatternsWorld: every write routes through the
 * INarr_StoryConditionSource (Seam/Narr_StoryConditionSource.h), whose authority-guarded ApplyFlag /
 * ApplyCounter are safe no-ops on clients (the canonical state replicates back through the world hub).
 *
 * The condition base (UNarr_Condition) and its read leaves live in Logic/Narr_Condition.h; the source
 * interface lives in Seam/Narr_StoryConditionSource.h. This header owns ONLY the effect side, so there is
 * exactly one definition of each shared type across the module.
 *
 * DECLARED-FOR-SIBLINGS: the dialogue area depends on UNarr_Effect (base) defined here.
 */

/**
 * Base class for an inline, composable narrative effect.
 *
 * Authored inline (EditInlineNew, Instanced) inside story-beat unlock lists and dialogue node/choice
 * effect lists. Applied through an INarr_StoryConditionSource so writes route to the authoritative world
 * hub (no-ops on clients). Subclass and override Apply_Implementation; the base does nothing.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNSNARRATIVE_API UNarr_Effect : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Apply this effect through Source. AUTHORITY-side semantics are enforced by the Source
	 * implementation (the world-hub write path no-ops on clients), so effects do not re-check authority.
	 * A null/invalid source is a safe no-op.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Narrative|Effect")
	void Apply(const TScriptInterface<INarr_StoryConditionSource>& Source) const;
	virtual void Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const;

	/** Human-readable summary for debug/log/editor tooltips. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Effect")
	virtual FString DescribeEffect() const;

protected:
	/** Resolve the raw native source pointer from a TScriptInterface, or null. */
	static INarr_StoryConditionSource* GetSource(const TScriptInterface<INarr_StoryConditionSource>& Source);
};

/** Effect: set a world-hub boolean flag to a fixed value. AUTHORITY ONLY (via the source). */
UCLASS(meta = (DisplayName = "Set Flag"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Effect_SetFlag : public UNarr_Effect
{
	GENERATED_BODY()

public:
	/** The world-hub flag key to write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect", meta = (Categories = "DP.WorldHub"))
	FGameplayTag FlagKey;

	/** The value to set the flag to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	bool bValue = true;

	virtual void Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeEffect() const override;
};

/** Effect: add a (possibly negative) delta to a world-hub counter. AUTHORITY ONLY (via the source). */
UCLASS(meta = (DisplayName = "Add To Counter"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Effect_AddCounter : public UNarr_Effect
{
	GENERATED_BODY()

public:
	/** The world-hub counter key to mutate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect", meta = (Categories = "DP.WorldHub"))
	FGameplayTag CounterKey;

	/** The amount to add (may be negative). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	int64 Delta = 1;

	virtual void Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeEffect() const override;
};
