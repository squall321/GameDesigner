// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Narr_StoryBeatDataAsset.generated.h"

class UNarr_Condition;
class UNarr_Effect;
class INarr_StoryConditionSource;

/**
 * Designer-authored definition of a single STORY BEAT.
 *
 * A beat is the atomic unit of branching story progression. It is identified by a stable beat tag
 * (its DataTag, anchored under Narr.Beat.*), belongs to a story arc (ArcTag, under Narr.Arc.*), is
 * gated by inline UNarr_Condition prerequisites (the SAME condition mini-language the dialogue runner
 * uses — Logic/Narr_Condition.h), applies inline UNarr_Effect unlocks when it completes (Logic/
 * Narr_Effect.h), and names the candidate NEXT beats the director may advance to.
 *
 * The beat is PURE CONTENT — it holds no runtime state. Runtime tracking (which beats are active /
 * completed) lives in UNarr_StoryDirectorSubsystem; the durable story FLAGS themselves live in the
 * World hub (which replicates them). This asset only describes the graph and its gates/effects.
 *
 * Authoring notes:
 *   - Prerequisites are evaluated read-only against an INarr_StoryConditionSource (the story
 *     director), exactly like dialogue gates, so a beat and a dialogue node can share conditions.
 *   - CompletionEffects are applied with a WorldContext (the director's owning game instance world);
 *     hub-writing effects self-guard authority and no-op on clients.
 *   - NextBeats are CANDIDATES; the director activates the first whose own prerequisites pass (or, with
 *     bAdvanceAllEligibleNext, every eligible candidate) so a beat can branch.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSNARRATIVE_API UNarr_StoryBeatDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UNarr_StoryBeatDataAsset();

	/**
	 * The story arc this beat belongs to (Narr.Arc.*). The director groups beats by arc to raise
	 * ArcStarted/ArcCompleted events and to drive per-arc save/restore.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat", meta = (Categories = "Narr.Arc"))
	FGameplayTag ArcTag;

	/** Optional designer-facing description of the beat for tooling / debug overlays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat", meta = (MultiLine = true))
	FText Description;

	/**
	 * Inline prerequisites that ALL must pass before the director will activate this beat. An empty
	 * list means "no gate" (always eligible). Each entry is a condition tree; author Any/Not logic with
	 * the composite leaves. Evaluated read-only against the story director.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat")
	TArray<TObjectPtr<UNarr_Condition>> Prerequisites;

	/**
	 * Inline effects applied (authority side) when this beat COMPLETES — e.g. set a world-hub story
	 * flag, bump a counter, raise a bus event. Applied through the standard UNarr_Effect::Apply path
	 * (authority self-guarded). Beat completion itself also sets the canonical "beat completed" hub flag.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat")
	TArray<TObjectPtr<UNarr_Effect>> CompletionEffects;

	/**
	 * Candidate beats the director may advance to after this beat completes. Each candidate is itself
	 * gated by ITS OWN Prerequisites, so this list expresses the branch fan-out; the director resolves
	 * which candidate(s) actually activate. Tags reference other beat assets by their DataTag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat", meta = (Categories = "Narr.Beat"))
	TArray<FGameplayTag> NextBeats;

	/**
	 * When true, on completion the director activates EVERY eligible NextBeat (parallel branches);
	 * when false it activates only the FIRST eligible candidate (exclusive branch). Default exclusive.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat")
	bool bAdvanceAllEligibleNext = false;

	/**
	 * When true this beat completes automatically the moment it activates (a pass-through / router
	 * beat used purely to fan out to NextBeats). When false the beat stays active until gameplay calls
	 * CompleteBeat. Default false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat")
	bool bAutoCompleteOnActivate = false;

	/**
	 * Optional explicit world-hub flag key set true when this beat is COMPLETED. When unset, the
	 * director derives a canonical key from the beat tag, so designers usually leave this empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Beat", meta = (Categories = "DP.WorldHub"))
	FGameplayTag CompletionFlagOverride;

	/**
	 * Evaluate every prerequisite against Source. @return true if all pass (or there are none).
	 * A null entry is treated as an absent gate (passes).
	 */
	bool ArePrerequisitesMet(const TScriptInterface<INarr_StoryConditionSource>& Source) const;

	/**
	 * Apply every completion effect through Source. Effects route writes through the source's
	 * authority-guarded API, so this is safe to call from any machine (no-op on clients).
	 */
	void ApplyCompletionEffects(const TScriptInterface<INarr_StoryConditionSource>& Source) const;

	//~ Begin UDP_DataAsset
	/** Collapse all story beats into one asset-manager bucket ("Narr_StoryBeat"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
